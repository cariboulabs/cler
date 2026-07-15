// Slim "Spike-like" spectrum analyzer GUI for USRP on CLER. All blocks always
// run; a staged rate change re-syncs every consumer once the ACTUAL hardware rate lands.

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/sources/source_hackrf.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/triggers/trigger_block.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "power_detector.hpp"
#include "channelizer_panel.hpp"

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using Trig = TriggerBlock<float>;
using PowerDet = PowerDetectorBlock<std::complex<float>>;

// ---------------------------------------------------------------------------
// Source abstraction
//
// spike was hardwired to SourceUHDBlock. To also drive a HackRF (whose gain
// model is LNA/VGA/amp rather than a single dB knob, and which has no live
// sample-rate change) without duplicating the whole GUI, the app talks to the
// device only through this narrow interface. Two adapters implement it:
//   - UHDSourceAdapter  : forwards to SourceUHDBlock's staged request_configure()
//   - HackRFSourceAdapter: calls SourceHackRFBlock's direct setters
// The concrete block is still built in main() and handed to the flowgraph; the
// adapter is a thin, non-owning view used by ControlPanel/main.
// ---------------------------------------------------------------------------
enum class SourceKind { UHD, HackRF };

// Superset config carrying both gain models; each adapter uses only the fields
// its device understands (UHD: gain_db, rate/bw; HackRF: lna/vga/amp, freq).
struct SourceConfig {
    double center_freq_Hz = 915e6;
    double sample_rate_Hz = 1e6;
    double bandwidth_Hz   = 1e6;
    double gain_db        = 30.0;   // UHD single-knob gain
    int    lna_gain_db    = 40;     // HackRF LNA (0-40, step 8)
    int    vga_gain_db    = 16;     // HackRF VGA (0-62, step 2)
    bool   amp_enable     = false;  // HackRF RX amp (~+14 dB)
};

struct ISource {
    virtual ~ISource() = default;
    // Actual running sample rate (device may snap the request). HackRF has no
    // live rate change, so this just echoes the constructed rate.
    virtual double actual_sample_rate() const = 0;
    // Stage/apply a configuration. Callable from the GUI thread.
    virtual void   request_configure(const SourceConfig& cfg) = 0;
    virtual size_t get_overflow_count() const = 0;
    virtual SourceKind kind() const = 0;
};

// UHD: everything is staged through the block's own thread-safe request path.
struct UHDSourceAdapter : public ISource {
    explicit UHDSourceAdapter(SourceUHDBlock<std::complex<float>>* src) : _src(src) {}
    double actual_sample_rate() const override { return _src->actual_sample_rate(); }
    void   request_configure(const SourceConfig& cfg) override {
        UHDConfig u;
        u.center_freq_Hz = cfg.center_freq_Hz;
        u.sample_rate_Hz = cfg.sample_rate_Hz;
        u.gain           = cfg.gain_db;
        u.bandwidth_Hz   = cfg.bandwidth_Hz;
        _src->request_configure(u);
    }
    size_t get_overflow_count() const override { return _src->get_overflow_count(); }
    SourceKind kind() const override { return SourceKind::UHD; }
private:
    SourceUHDBlock<std::complex<float>>* _src;
};

// HackRF: libhackrf's control transfers are separate from the RX callback, so
// the setters are safe to call directly from the GUI thread while streaming.
// Sample rate is fixed at construction (the block exposes no rate setter), so
// request_configure() only touches frequency and the LNA/VGA/amp gain stages.
struct HackRFSourceAdapter : public ISource {
    explicit HackRFSourceAdapter(SourceHackRFBlock* src) : _src(src) {}
    double actual_sample_rate() const override {
        return static_cast<double>(_src->get_sample_rate());
    }
    void request_configure(const SourceConfig& cfg) override {
        _src->set_frequency(static_cast<uint64_t>(cfg.center_freq_Hz + 0.5));
        // Snap to the hardware's legal steps before applying.
        int lna = std::min(std::max(cfg.lna_gain_db, 0), 40);
        lna = (lna / 8) * 8;
        int vga = std::min(std::max(cfg.vga_gain_db, 0), 62);
        vga = (vga / 2) * 2;
        _src->set_lna_gain(lna);
        _src->set_vga_gain(vga);
        _src->set_amp_enable(cfg.amp_enable);
    }
    size_t get_overflow_count() const override { return _src->get_overflow_count(); }
    SourceKind kind() const override { return SourceKind::HackRF; }
private:
    SourceHackRFBlock* _src;
};

static void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

struct SpikeArgs {
    SourceKind source = SourceKind::UHD;   // --source uhd|hackrf (default uhd)
    double freq = 915e6;
    double rate = 1e6;
    double gain = 30.0;             // UHD single-knob gain (dB)
    int    lna  = 40;              // HackRF LNA gain (0-40, step 8)
    int    vga  = 16;              // HackRF VGA gain (0-62, step 2)
    bool   amp  = false;           // HackRF RX amp
    size_t fft  = 2048;
    bool   rate_from_cli = false;   // -r given explicitly: overrides saved rate_hz
    std::string device_address;
};

static void print_usage(const char* prog) {
    std::cout << "\nSlim Spike-like analyzer for USRP / HackRF\n"
              << "Usage: " << prog << " [OPTIONS]\n"
              << "  -s, --source DEV  Device source: uhd|hackrf (default uhd)\n"
              << "  -f, --freq FREQ   Center frequency Hz (default 915e6)\n"
              << "  -r, --rate RATE   Initial sample rate S/s (default 1e6; live-tunable\n"
              << "                    in the GUI on UHD only; if given, overrides saved rate)\n"
              << "  -g, --gain GAIN   [uhd]    Gain dB (default 30)\n"
              << "      --lna  DB     [hackrf] LNA gain dB, 0-40 step 8 (default 40)\n"
              << "      --vga  DB     [hackrf] VGA gain dB, 0-62 step 2 (default 16)\n"
              << "      --amp  0|1    [hackrf] RX amp on/off (default 0)\n"
              << "  -F, --fft  SIZE   FFT size for spectrum view (default 2048)\n"
              << "  -d, --dev  ADDR   [uhd] USRP device address (default auto)\n"
              << "  -h, --help\n" << std::endl;
}

static SpikeArgs parse_args(int argc, char** argv) {
    SpikeArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " needs a value\n"; exit(1); }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); exit(0); }
        else if (arg == "-s" || arg == "--source") {
            std::string v = next();
            if (v == "uhd" || v == "usrp") a.source = SourceKind::UHD;
            else if (v == "hackrf")        a.source = SourceKind::HackRF;
            else { std::cerr << "Unknown source: " << v << " (want uhd|hackrf)\n"; exit(1); }
        }
        else if (arg == "-f" || arg == "--freq") a.freq = std::stod(next());
        else if (arg == "-r" || arg == "--rate") { a.rate = std::stod(next()); a.rate_from_cli = true; }
        else if (arg == "-g" || arg == "--gain") a.gain = std::stod(next());
        else if (arg == "--lna") a.lna = std::stoi(next());
        else if (arg == "--vga") a.vga = std::stoi(next());
        else if (arg == "--amp") a.amp = (std::stoi(next()) != 0);
        else if (arg == "-F" || arg == "--fft")  a.fft  = std::stoul(next());
        else if (arg == "-d" || arg == "--dev" || arg == "--device") a.device_address = next();
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); exit(1); }
    }
    return a;
}

// Render-only: writes staged source config and mutex-guarded trigger config; owns neither.
struct ControlPanel {
    ControlPanel(ISource* src, Trig* trig,
                 PlotCSpectrumBlock* spectrum,
                 PlotCSpectrogramBlock* sgram,
                 PowerDetectorBlock<std::complex<float>>* power,
                 ChannelizerPanelBlock* chan,
                 size_t sgram_rows, const SpikeArgs& a)
        : _src(src), _trig(trig), _spectrum(spectrum), _sgram(sgram), _power(power),
          _chan(chan),
          _kind(src->kind()),
          _freq_ui_max_mhz(src->kind() == SourceKind::HackRF ? 7250.0f : 6000.0f),
          _freq_mhz(static_cast<float>(a.freq / 1e6)),
          _freq_anchor_mhz(_freq_mhz),
          _gain_db(static_cast<float>(a.gain)),
          _lna_db(a.lna),
          _vga_db(a.vga),
          _amp_enable(a.amp),
          _rate_hz(a.rate),
          _rate_msps(static_cast<float>(a.rate / 1e6)),
          _rate_from_cli(a.rate_from_cli),
          _n_fft(a.fft),
          _sgram_rows(sgram_rows),
          _zs_bw_mhz(static_cast<float>(a.rate / 1e6)),   // full rate = bypass
          _max_window_ms(trig->max_window_ms()) {
        _history_s = fpr_to_history(8);   // default depth; conf key history_s overrides
    }

    // Slider/drag that becomes a typed input on double-click; [tmin,tmax] (default vmin/vmax) bounds the typed value.
    bool editable(const char* label, float* v, float vmin, float vmax,
                  const char* fmt, bool use_slider, float drag_speed = 1.0f,
                  float tmin = NAN, float tmax = NAN) {
        if (std::isnan(tmin)) tmin = vmin;
        if (std::isnan(tmax)) tmax = vmax;
        bool changed = false;
        if (_editing == label) {
            if (_editing_start) { ImGui::SetKeyboardFocusHere(); _editing_start = false; }
            changed = ImGui::InputFloat(label, v, 0.0f, 0.0f, fmt,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivated()) {        // Enter or clicked away: commit
                *v = std::min(std::max(*v, tmin), tmax);
                _editing = nullptr;
                changed = true;
            }
        } else {
            if (use_slider)
                changed = ImGui::SliderFloat(label, v, vmin, vmax, fmt, ImGuiSliderFlags_AlwaysClamp);
            else
                changed = ImGui::DragFloat(label, v, drag_speed, vmin, vmax, fmt, ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _editing = label;        // string literals have stable addresses
                _editing_start = true;
            }
        }
        return changed;
    }

    // Read-only display; double-click opens a typed input clamped to [vmin, vmax].
    bool grid_field(const char* label, float* v, const char* fmt,
                    float vmin, float vmax) {
        bool changed = false;
        if (_editing == label) {
            if (_editing_start) { ImGui::SetKeyboardFocusHere(); _editing_start = false; }
            ImGui::SetNextItemWidth(120);
            ImGui::InputFloat(label, v, 0.0f, 0.0f, fmt,
                              ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivated()) {        // Enter or clicked away: commit
                *v = std::min(std::max(*v, vmin), vmax);
                _editing = nullptr;
                changed = true;
            }
        } else {
            // Button/InputFloat need DIFFERENT ids ("_btn" suffix) or the button's active state deactivates the input.
            char btn[96];
            int n = std::snprintf(btn, sizeof(btn), fmt, static_cast<double>(*v));
            if (n < 0) n = 0;
            if (n > static_cast<int>(sizeof(btn)) - 1) n = sizeof(btn) - 1;
            std::snprintf(btn + n, sizeof(btn) - static_cast<size_t>(n),
                          "###%s_btn", label);
            ImGui::Button(btn, ImVec2(120, 0));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _editing = label;        // string literals have stable addresses
                _editing_start = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        }
        return changed;
    }

    void render() {
        // Polled every frame: the source snaps the requested rate async, so re-sync everything once it lands.
        {
            const double actual = _src->actual_sample_rate();
            if (std::abs(actual - _rate_hz) > 0.5) on_rate_changed(actual);
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        // Tiling code below reads the ACTUAL rect (window_pos/window_size), not a fixed strip.
        ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f),
                                            ImVec2(420.0f, FLT_MAX));
        ImGui::Begin("Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextUnformatted("View");
        ImGui::Separator();
        ImGui::Checkbox("Zero-span scope", &show_scope);
        ImGui::SameLine();
        ImGui::Checkbox("Spectrum", &show_spectrum);
        ImGui::Checkbox("Spectrogram (waterfall)", &show_spectrogram);
        ImGui::SameLine();
        ImGui::Checkbox("Channelizer", &show_channelizer);
        help("Which windows to display. All views run continuously; these only "
             "toggle visibility, so nothing needs restarting.");

        if (ImGui::Button("Arrange windows")) arrange_requested = true;
        help("Re-tile the visible plot windows into equal-height rows next to "
             "the control panel. Also happens automatically when the View "
             "checkboxes change or the main window is resized. You can still "
             "move/resize windows afterward.");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Snapshot");
        ImGui::Separator();
        if (ImGui::Button("Snapshot")) {
            if (_snapshot_dir.empty()) {
                const char* home = std::getenv("HOME");
                std::snprintf(_snapdir_edit, sizeof(_snapdir_edit), "%s",
                              home ? home : ".");
                ImGui::OpenPopup("Snapshot directory");
            } else {
                snapshot_requested = true;
            }
        }
        help("Save a screenshot of the whole window (.bmp) plus the data behind "
             "the currently visible plots (.dat, self-describing text with a "
             "binary spectrogram blob) into the snapshot directory. The "
             "directory is asked for once and remembered in the conf file.");
        if (ImGui::BeginPopupModal("Snapshot directory", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Directory to save snapshots into:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##snapdir", _snapdir_edit, sizeof(_snapdir_edit));
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                _snapshot_dir = trimmed(_snapdir_edit);
                if (!_snapshot_dir.empty()) snapshot_requested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!_status.empty() &&
            std::chrono::steady_clock::now() < _status_until) {
            ImGui::TextWrapped("%s", _status.c_str());
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Radio");
        ImGui::Separator();

        // Rate is only REQUESTED here; the poll above re-syncs this widget once
        // it lands. HackRF has no live rate change, so the widget is read-only.
        const bool rate_live = (_kind == SourceKind::UHD);
        ImGui::BeginDisabled(!rate_live);
        ImGui::SetNextItemWidth(180);
        editable("Rate (MS/s)", &_rate_msps, 0.1f, 61.44f, "%.3f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        if (rate_live && ImGui::IsItemDeactivatedAfterEdit())
            push_rate_config(static_cast<double>(_rate_msps) * 1e6);
        ImGui::EndDisabled();
        if (rate_live)
            help("Sample rate = displayed span, applied live. The hardware may "
                 "snap the request to what its clocking supports; the shown value "
                 "follows the ACTUAL device rate. Note: a capture straddling the "
                 "switch may be garbled once, and the waterfall restarts.");
        else
            help("Sample rate = displayed span. Fixed at startup on HackRF "
                 "(no live rate change); set it with --rate on the command line.");

        // Apply only on edit-end (avoid spamming retunes); anchor recenters on commit only, else mid-drag moves the mapping under the cursor.
        const float freq_lo = std::max(0.0f,            _freq_anchor_mhz - 25.0f);
        const float freq_hi = std::min(_freq_ui_max_mhz, _freq_anchor_mhz + 25.0f);
        ImGui::SetNextItemWidth(180);
        editable("Center (MHz)", &_freq_mhz, freq_lo, freq_hi, "%.3f",
                 /*slider=*/true, 0.1f, /*tmin=*/0.0f, /*tmax=*/_freq_ui_max_mhz);
        bool freq_done = ImGui::IsItemDeactivatedAfterEdit();
        if (freq_done) {
            _freq_anchor_mhz = _freq_mhz;   // recenter for next time
            _chan->set_center_freq(static_cast<double>(_freq_mhz) * 1e6);
        }

        // Gain: UHD is a single dB knob; HackRF splits into LNA/VGA/amp stages.
        bool gain_done = false;
        if (_kind == SourceKind::UHD) {
            ImGui::SetNextItemWidth(180);
            editable("Gain (dB)", &_gain_db, 0.0f, 76.0f, "%.1f", /*slider=*/true);
            gain_done = ImGui::IsItemDeactivatedAfterEdit();
        } else {
            ImGui::SetNextItemWidth(180);
            ImGui::SliderInt("LNA (dB)", &_lna_db, 0, 40, "%d");
            gain_done |= ImGui::IsItemDeactivatedAfterEdit();
            help("HackRF front-end LNA gain. Applied in 8 dB steps (0-40).");
            ImGui::SetNextItemWidth(180);
            ImGui::SliderInt("VGA (dB)", &_vga_db, 0, 62, "%d");
            gain_done |= ImGui::IsItemDeactivatedAfterEdit();
            help("HackRF baseband VGA gain. Applied in 2 dB steps (0-62).");
            if (ImGui::Checkbox("RX amp (+14 dB)", &_amp_enable)) gain_done = true;
            help("HackRF external RX amplifier: adds ~14 dB but can overload on "
                 "strong signals.");
        }
        if (freq_done || gain_done) push_radio_config();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Trigger");
        ImGui::Separator();

        bool changed = false;
        changed |= editable("Level (dB)", &_threshold_db, -120.0f, 0.0f, "%.1f", true);
        help("Power level the signal must cross to fire a trigger.");
        changed |= editable("Hysteresis (dB)", &_hysteresis_db, 0.0f, 30.0f, "%.1f", true);
        help("Dead-band. After a rising trigger fires, the signal must drop below "
             "(Level - Hysteresis) before it can fire again. Stops chatter when the "
             "signal hovers right at the Level.");
        ImGui::SetNextItemWidth(150);
        changed |= editable("Window (ms)", &_window_ms, 1.0f, _max_window_ms, "%.1f", true);
        ImGui::SameLine();
        // Capture memory is fixed at startup, so this shrinks at higher rates (refreshed by on_rate_changed()).
        ImGui::TextDisabled("max %.0f", _max_window_ms);
        help("Total time span captured and displayed per trigger (pre + post). "
             "This is the scope timebase. To see a whole repeating burst locked in "
             "place, set Window a bit LESS than the burst period and >= the burst "
             "span. 'max' is the largest window at the CURRENT sample rate: the "
             "capture buffer is sized once at startup, so raising the rate "
             "shrinks the reachable window (in ms).");
        changed |= editable("Pre-trigger (%)", &_pretrigger_pct, 0.0f, 90.0f, "%.0f", true);
        help("How much of the Window is shown BEFORE the trigger instant (t=0). "
             "e.g. 10% puts the trigger 10% in from the left.");
        changed |= editable("Holdoff (ms)", &_holdoff_ms, 0.0f, 5000.0f, "%.0f", true);
        help("Minimum time AFTER a capture finishes before another trigger can fire "
             "(added on top of the Window time, not overlapping). To lock a repeating "
             "burst, set Window < burst period, then keep Holdoff small.");
        changed |= editable("Auto timeout (ms)", &_auto_ms, 10.0f, 2000.0f, "%.0f", true);
        help("Auto mode only: if no edge arrives within this time, fire anyway so the "
             "display keeps refreshing. Ignored in Normal/Single.");

        const char* edges[] = {"Rising", "Falling"};
        changed |= ImGui::Combo("Edge", &_edge_idx, edges, 2);
        help("Rising: fire when power crosses Level upward. Falling: downward.");
        const char* modes[] = {"Normal", "Single", "Auto"};
        changed |= ImGui::Combo("Mode", &_mode_idx, modes, 3);
        help("Normal: capture on every trigger. Single: capture one then stop "
             "(press Arm to go again). Auto: like Normal but free-runs if idle.");

        if (changed) push_trigger_config();

        // Push decimator config before trigger: trigger runs at the DETECTION rate (decimated when filter on, device rate off).
        const bool filter_toggled = ImGui::Checkbox("Filter", &_zs_filter_on);
        help("Enable the zero-span channel-selection filter. Off (default): "
             "the power trace sees the raw full-rate I/Q stream, exactly as "
             "without a filter. On: the stream is decimated to the bandwidth "
             "below before the power is computed.");
        ImGui::BeginDisabled(!_zs_filter_on);
        ImGui::SetNextItemWidth(180);
        editable("Zero-span BW (MHz)", &_zs_bw_mhz, min_zs_bw_mhz(),
                 static_cast<float>(_rate_hz / 1e6), "%.4f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        const bool bw_committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (!_zs_filter_on) {
            ImGui::TextDisabled("(off)");
        } else {
            const size_t R = PowerDet::decimation_for(zs_bw_hz(), _rate_hz);
            if (R <= 1) {
                ImGui::TextDisabled("(bypass)");
            } else {
                const double eff = _rate_hz / static_cast<double>(R);
                if (eff < 1e6) ImGui::TextDisabled("eff. %.1f kHz", eff / 1e3);
                else           ImGui::TextDisabled("eff. %.3f MHz", eff / 1e6);
            }
        }
        ImGui::EndDisabled();
        if (filter_toggled || bw_committed) {
            clamp_zs_bw();
            push_power_config();
            refresh_trigger_window();
            push_trigger_config();
        }
        help("Channel (detection) bandwidth of the zero-span power trace: the "
             "I/Q stream is decimated by an integer factor R with an 80 dB "
             "anti-alias lowpass, so exactly this two-sided width around the "
             "center survives and off-channel bursts are rejected before the "
             "power is computed. The achieved value snaps to sample_rate/R -- "
             "'eff.' shows what is actually applied. The trigger then runs at "
             "the decimated rate, so narrower bandwidths allow LONGER capture "
             "windows. A capture straddling a toggle or bandwidth change may "
             "be garbled once.");

        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Arm / Re-arm")) _trig->rearm();
        ImGui::SameLine();
        if (ImGui::Button("Force"))        _trig->force_trigger();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("State: %s", state_str(_trig->state()));

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Channelizer");
        ImGui::Separator();

        // Snaps to N = rate/width integer channels; effective width/N mirrored beside the control.
        ImGui::SetNextItemWidth(180);
        editable("Channel width (MHz)", &_chan_width_mhz, min_chan_width_mhz(),
                 static_cast<float>(_rate_hz / 2e6), "%.3f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        const bool chan_width_committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        {
            const size_t n = ChannelizerPanelBlock::channels_for(
                chan_width_hz(), _rate_hz);
            const double eff = ChannelizerPanelBlock::effective_width(
                chan_width_hz(), _rate_hz);
            ImGui::TextDisabled("N=%zu eff. %.3f MHz", n, eff / 1e6);
        }
        help("Width of each channelizer strip. The device rate is split into "
             "N = rate/width equal channels (N snaps to an integer in [2, 64]; "
             "'eff.' shows the width actually applied). Each strip is a "
             "scrolling peak-power-vs-time trace of one channel; strips are "
             "labeled with their absolute center frequency. A width or rate "
             "change restarts the strip history.");
        ImGui::SetNextItemWidth(180);
        editable("Span (s)", &_chan_span_s, 1.0f, 600.0f, "%.1f",
                 /*slider=*/false, /*drag_speed=*/0.1f);
        const bool chan_span_committed = ImGui::IsItemDeactivatedAfterEdit();
        help("Time depth of the channelizer strips (shared X axis). Longer "
             "spans peak-hold more samples into each on-screen point.");
        if (chan_width_committed || chan_span_committed) {
            clamp_chan_width();
            push_chan_config();
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Spectrogram grid");
        ImGui::Separator();
        // Grid on/off toggle lives in the Spectrogram window; here only sets line spacing.
        if (grid_field("Grid time (ms)", &_grid_time_ms, "%.1f",
                       0.1f, 100000.0f))
            _sgram->set_grid_time_ms(_grid_time_ms);
        help("Spacing between horizontal (time) grid lines on the waterfall, in "
             "milliseconds. Double-click the field to type a value. Turn the "
             "grid on/off with the 'Grid' checkbox in the Spectrogram window.");
        if (grid_field("Grid freq (MHz)", &_grid_freq_mhz, "%.4f",
                       0.0001f, 100000.0f))
            _sgram->set_grid_freq_mhz(_grid_freq_mhz);
        help("Spacing between vertical (frequency) grid lines on the waterfall, "
             "in MHz, anchored at the band center (DC). Double-click to type a "
             "value.");

        // window_pos/size feed the main loop's tiling (auto-sized, so not constant).
        window_pos  = ImGui::GetWindowPos();
        window_size = ImGui::GetWindowSize();

        ImGui::End();
    }

    // Transient status line under the Snapshot button (auto-hides).
    void set_status(const std::string& s) {
        _status = s;
        _status_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    }

    const std::string& snapshot_dir() const { return _snapshot_dir; }
    float  freq_mhz() const { return _freq_mhz; }
    double rate_hz()  const { return _rate_hz; }

    // Rate the TRIGGER's power stream runs at (device rate / decimation when the filter is on); all trigger ms<->samples math must use this.
    size_t detection_rate() const {
        const size_t R = _zs_filter_on
                       ? PowerDet::decimation_for(zs_bw_hz(), _rate_hz)
                       : 1;
        double det = std::round(_rate_hz / static_cast<double>(R));
        if (det < 1.0) det = 1.0;
        return static_cast<size_t>(det);
    }

    // Push loaded/initial settings to radio, trigger, and spectrogram (startup).
    void apply_all() {
        push_radio_config();
        // Decimator must land before trigger config: trigger's time base is the DECIMATED rate.
        clamp_zs_bw();
        push_power_config();
        refresh_trigger_window();
        push_trigger_config();
        clamp_chan_width();
        push_chan_config();
        _sgram->set_frames_per_row(history_to_fpr(_history_s));
        _sgram->set_show_grid(_show_grid);
        _sgram->set_grid_time_ms(_grid_time_ms);
        _sgram->set_grid_freq_mhz(_grid_freq_mhz);
        // Saved rate staged LAST via the live path so on_rate_changed() fires once it lands; explicit CLI -r wins over it.
        // HackRF has no live rate change, so its rate is whatever was constructed.
        if (_kind == SourceKind::UHD && !_rate_from_cli && _loaded_rate_hz > 0.0 &&
            std::abs(_loaded_rate_hz - _rate_hz) > 0.5) {
            push_rate_config(_loaded_rate_hz);
            // Seed above used the CONSTRUCTION rate; mark stale so on_rate_changed() redoes it.
            _history_seed_pending = true;
        }
    }

    // Which windows to draw; public so main()'s render loop can gate on them.
    bool show_scope       = true;
    bool show_spectrum    = true;
    bool show_spectrogram = false;
    bool show_channelizer = false;

    bool arrange_requested = false;   // one-shot from "Arrange windows"; consumed by main()'s tiling code
    bool snapshot_requested = false;  // one-shot from "Snapshot"; consumed by main() (has block/GuiManager scope)

    // Actual panel rect from the last render() (auto-sized width); main() tiles plots right of it, using defaults until then.
    ImVec2 window_pos  = ImVec2(10.0f, 10.0f);
    ImVec2 window_size = ImVec2(360.0f, 520.0f);

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string key;
        while (f >> key) {
            if      (key == "freq_mhz")       f >> _freq_mhz;
            else if (key == "gain_db")        f >> _gain_db;
            else if (key == "lna_gain_db")    f >> _lna_db;
            else if (key == "vga_gain_db")    f >> _vga_db;
            else if (key == "amp_enable")     f >> _amp_enable;
            else if (key == "threshold_db")   f >> _threshold_db;
            else if (key == "hysteresis_db")  f >> _hysteresis_db;
            else if (key == "window_ms")      f >> _window_ms;
            else if (key == "pretrigger_pct") f >> _pretrigger_pct;
            else if (key == "holdoff_ms")     f >> _holdoff_ms;
            else if (key == "auto_ms")        f >> _auto_ms;
            else if (key == "edge")           f >> _edge_idx;
            else if (key == "mode")           f >> _mode_idx;
            else if (key == "show_scope")     f >> show_scope;
            else if (key == "show_spectrum")  f >> show_spectrum;
            else if (key == "show_spectrogram") f >> show_spectrogram;
            else if (key == "show_channelizer") f >> show_channelizer;
            else if (key == "chan_width_mhz")   f >> _chan_width_mhz;
            else if (key == "chan_span_s")      f >> _chan_span_s;
            else if (key == "history_s")      f >> _history_s;
            else if (key == "grid_on")        f >> _show_grid;
            else if (key == "grid_time_ms")   f >> _grid_time_ms;
            else if (key == "grid_freq_mhz")  f >> _grid_freq_mhz;
            else if (key == "zs_bw_hz") {
                // Stored in Hz (widget shows MHz); key alone doesn't enable filtering (zs_filter_on does).
                float bw_hz = 0.0f;
                f >> bw_hz;
                _zs_bw_mhz = bw_hz / 1e6f;
            }
            else if (key == "zs_filter_on")   f >> _zs_filter_on;
            else if (key == "rate_hz")        f >> _loaded_rate_hz;
            else if (key == "snapshot_dir") {
                // Rest-of-line value (may contain spaces), unlike other whitespace-delimited keys.
                std::string rest;
                std::getline(f, rest);
                _snapshot_dir = trimmed(rest.c_str());
            }
            else { std::string skip; std::getline(f, skip); }
        }
        // Window is NOT clamped here: its limit depends on the DECIMATED rate, only known once bandwidth applies (clamped in apply_all()/on_rate_changed()).
        clamp_zs_bw();
        _chan_span_s = std::min(std::max(_chan_span_s, 1.0f), 600.0f);
        _freq_anchor_mhz = _freq_mhz;   // center the freq slider on the saved value
        return true;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "freq_mhz "       << _freq_mhz       << "\n"
          << "gain_db "        << _gain_db        << "\n"
          << "lna_gain_db "    << _lna_db         << "\n"
          << "vga_gain_db "    << _vga_db         << "\n"
          << "amp_enable "     << _amp_enable     << "\n"
          << "threshold_db "   << _threshold_db   << "\n"
          << "hysteresis_db "  << _hysteresis_db  << "\n"
          << "window_ms "      << _window_ms      << "\n"
          << "pretrigger_pct " << _pretrigger_pct << "\n"
          << "holdoff_ms "     << _holdoff_ms     << "\n"
          << "auto_ms "        << _auto_ms        << "\n"
          << "edge "           << _edge_idx       << "\n"
          << "mode "           << _mode_idx       << "\n"
          << "show_scope "       << show_scope       << "\n"
          << "show_spectrum "    << show_spectrum    << "\n"
          << "show_spectrogram " << show_spectrogram << "\n"
          << "history_s "        << fpr_to_history(_sgram->frames_per_row()) << "\n"
          << "grid_on "          << _sgram->show_grid()  << "\n"
          << "grid_time_ms "     << _grid_time_ms        << "\n"
          << "grid_freq_mhz "    << _grid_freq_mhz       << "\n"
          << "zs_bw_hz "         << static_cast<double>(_zs_bw_mhz) * 1e6 << "\n"
          << "zs_filter_on "     << _zs_filter_on    << "\n"
          << "show_channelizer " << show_channelizer << "\n"
          << "chan_width_mhz "   << _chan_width_mhz  << "\n"
          << "chan_span_s "      << _chan_span_s     << "\n"
          << "rate_hz "          << _rate_hz         << "\n";
        if (!_snapshot_dir.empty())
            f << "snapshot_dir " << _snapshot_dir << "\n";
    }

private:
    static std::string trimmed(const char* s) {
        std::string v(s);
        size_t b = v.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        size_t e = v.find_last_not_of(" \t\r\n");
        return v.substr(b, e - b + 1);
    }

    static const char* state_str(Trig::State s) {
        switch (s) {
            case Trig::State::Idle:      return "IDLE (single done - re-arm)";
            case Trig::State::Armed:     return "ARMED";
            case Trig::State::Capturing: return "CAPTURING";
        }
        return "?";
    }

    // Fill a SourceConfig from the current UI state (both gain models); the
    // adapter uses only the fields its device understands.
    SourceConfig make_config(double rate_hz) const {
        SourceConfig cfg;
        cfg.center_freq_Hz = static_cast<double>(_freq_mhz) * 1e6;
        cfg.sample_rate_Hz = rate_hz;
        cfg.bandwidth_Hz   = rate_hz;
        cfg.gain_db        = static_cast<double>(_gain_db);
        cfg.lna_gain_db    = _lna_db;
        cfg.vga_gain_db    = _vga_db;
        cfg.amp_enable     = _amp_enable;
        return cfg;
    }

    void push_radio_config() {
        // current rate: UHD skips this no-op, HackRF ignores rate entirely
        _src->request_configure(make_config(_rate_hz));
    }

    // Stages a new rate onto the source's streaming thread; render()'s poll picks up the actual result. (UHD only)
    void push_rate_config(double rate_hz) {
        _src->request_configure(make_config(rate_hz));
    }

    // Single choke point when the ACTUAL device rate changes; re-derives all rate-dependent state.
    void on_rate_changed(double actual_hz) {
        const double old_rate_hz = _rate_hz;   // for seconds-preserving rescales
        _rate_hz   = actual_hz;
        _rate_msps = static_cast<float>(actual_hz / 1e6);   // widget display

        // Decimator first: trigger's time base below is the re-derived DECIMATED rate.
        clamp_zs_bw();
        push_power_config();

        // Capture memory is fixed size, so max window (ms) shrinks as detection rate rises; push one config so counts land together.
        refresh_trigger_window();
        push_trigger_config();

        clamp_chan_width();
        push_chan_config();

        // Spectrogram also clears its ring here -- old rows would be mislabeled at the new rate.
        const size_t sps = static_cast<size_t>(actual_hz + 0.5);
        _spectrum->set_sample_rate(sps);
        _sgram->set_sample_rate(sps);

        // Preserve on-screen seconds across rate changes: redo the pending startup seed at the real rate, else rescale by new/old rate.
        if (_history_seed_pending) {
            _sgram->set_frames_per_row(history_to_fpr(_history_s));
            _history_seed_pending = false;
        } else if (old_rate_hz > 0.0) {
            double fpr = std::round(static_cast<double>(_sgram->frames_per_row())
                                    * actual_hz / old_rate_hz);
            if (fpr < 1.0)   fpr = 1.0;
            if (fpr > 256.0) fpr = 256.0;
            _sgram->set_frames_per_row(static_cast<size_t>(fpr));
        }
    }

    // history_s (conf key) <-> frames/row, translated only at load/save (live control is the History slider); one row spans fpr*n_fft/sps seconds.
    size_t history_to_fpr(float history_s) const {
        double fpr = std::round(static_cast<double>(history_s) * _rate_hz
                                / (static_cast<double>(_n_fft)
                                   * static_cast<double>(_sgram_rows)));
        if (fpr < 1.0)   fpr = 1.0;
        if (fpr > 256.0) fpr = 256.0;
        return static_cast<size_t>(fpr);
    }
    float fpr_to_history(size_t fpr) const {
        return static_cast<float>(static_cast<double>(fpr)
                                  * static_cast<double>(_n_fft)
                                  * static_cast<double>(_sgram_rows) / _rate_hz);
    }

    // Requested zero-span bandwidth in Hz (the widget shows MHz).
    double zs_bw_hz() const { return static_cast<double>(_zs_bw_mhz) * 1e6; }

    // Decimation is capped (rate/Rmax); below this floor the request would snap far from typed, so clamp the widget instead.
    float min_zs_bw_mhz() const {
        const double floor_hz = std::max(PowerDet::MIN_OUTPUT_RATE_HZ,
                                         _rate_hz / static_cast<double>(PowerDet::MAX_DECIMATION));
        return static_cast<float>(std::min(floor_hz, _rate_hz) / 1e6);
    }

    void clamp_zs_bw() {
        const float hi = static_cast<float>(_rate_hz / 1e6);
        _zs_bw_mhz = std::min(std::max(_zs_bw_mhz, min_zs_bw_mhz()), hi);
    }

    // Re-derive the reachable window (fixed sample capacity in ms at the DETECTION rate) and re-clamp.
    void refresh_trigger_window() {
        _max_window_ms = 1000.0f * static_cast<float>(_trig->max_window_samples())
                       / static_cast<float>(detection_rate());
        if (_window_ms > _max_window_ms) _window_ms = _max_window_ms;
    }

    void push_power_config() {
        // Filter off requests the full rate; decimation_for() snaps that to R=1, a true bypass with no decimator object.
        const double bw = _zs_filter_on ? zs_bw_hz() : _rate_hz;
        _power->set_channel_bandwidth(bw, _rate_hz);
    }

    // Requested channelizer channel width in Hz (the widget shows MHz).
    double chan_width_hz() const { return static_cast<double>(_chan_width_mhz) * 1e6; }

    // N is capped at N_MAX; a narrower request would snap far from typed, so clamp (mirrors min_zs_bw_mhz).
    float min_chan_width_mhz() const {
        return static_cast<float>(
            _rate_hz / static_cast<double>(ChannelizerPanelBlock::N_MAX) / 1e6);
    }

    void clamp_chan_width() {
        const float hi = static_cast<float>(_rate_hz / 2e6);   // N >= 2
        _chan_width_mhz = std::min(std::max(_chan_width_mhz, min_chan_width_mhz()), hi);
    }

    void push_chan_config() {
        _chan->set_config(chan_width_hz(), _rate_hz,
                          static_cast<double>(_chan_span_s),
                          static_cast<double>(_freq_mhz) * 1e6);
    }

    void push_trigger_config() {
        _trig->set_config(_threshold_db, _window_ms, _pretrigger_pct,
                          _holdoff_ms,
                          _edge_idx == 0 ? Trig::Edge::Rising : Trig::Edge::Falling,
                          _mode_idx == 0 ? Trig::Mode::Normal
                                         : (_mode_idx == 1 ? Trig::Mode::Single
                                                           : Trig::Mode::Auto),
                          _hysteresis_db, _auto_ms,
                          detection_rate());
    }

    ISource* _src;
    Trig* _trig;
    PlotCSpectrumBlock* _spectrum;
    PlotCSpectrogramBlock* _sgram;
    PowerDetectorBlock<std::complex<float>>* _power;
    ChannelizerPanelBlock* _chan;

    SourceKind _kind;               // selects gain UI + rate-live behavior
    float  _freq_ui_max_mhz;        // freq slider/typed upper bound (6000 UHD, 7250 HackRF)
    float  _freq_mhz;
    float  _freq_anchor_mhz;        // center of the +/-25MHz slider window; moved only on commit (render())
    float  _gain_db;                // UHD single-knob gain
    int    _lna_db;                 // HackRF LNA (0-40, step 8)
    int    _vga_db;                 // HackRF VGA (0-62, step 2)
    bool   _amp_enable;             // HackRF RX amp
    double _rate_hz;                // the ACTUAL device rate last synced to
    float  _rate_msps;              // rate widget value (MS/s), follows _rate_hz
    double _loaded_rate_hz = 0.0;   // conf key rate_hz (0 = not present)
    bool   _rate_from_cli;          // -r given explicitly: overrides saved rate_hz
    size_t _n_fft;
    size_t _sgram_rows;
    float  _history_s = 0.0f;               // waterfall depth (s); seeds spectrogram in apply_all()
    bool   _history_seed_pending = false;   // seed used construction rate; on_rate_changed() must redo it
    // Waterfall grid (conf: grid_on/grid_time_ms/grid_freq_mhz); on/off flag lives in the spectrogram window, panel owns/pushes the densities.
    bool   _show_grid      = false;
    float  _grid_time_ms   = 100.0f;   // horizontal (time) line spacing, ms
    float  _grid_freq_mhz  = 1.0f;     // vertical (frequency) line spacing, MHz
    float  _zs_bw_mhz;          // zero-span channel bandwidth (MHz); rate = bypass
    bool   _zs_filter_on = false;   // engages filter only when checked (old confs = off)
    float  _chan_width_mhz = 1.0f;  // requested width (conf key chan_width_mhz); snapped to rate/N live
    float  _chan_span_s    = 10.0f; // channelizer strip time depth (chan_span_s)

    // Trigger UI state (defaults mirror the TriggerBlock constructor below).
    float _threshold_db   = -40.0f;
    float _hysteresis_db  = 3.0f;
    float _window_ms      = 20.0f;
    float _pretrigger_pct = 10.0f;
    float _holdoff_ms     = 100.0f;
    float _auto_ms        = 200.0f;
    float _max_window_ms  = 200.0f;
    int   _edge_idx       = 0;  // Rising
    int   _mode_idx       = 2;  // Auto

    // Double-click-to-type state: which field (by label pointer) is being typed.
    const char* _editing  = nullptr;
    bool        _editing_start = false;

    // Snapshot: dest dir (conf key snapshot_dir, asked once via modal), edit buffer, transient status.
    std::string _snapshot_dir;
    char        _snapdir_edit[512] = {0};
    std::string _status;
    std::chrono::steady_clock::time_point _status_until{};
};

static std::string config_path(const char* leaf) {
    const char* home = std::getenv("HOME");
    std::string dir = home ? std::string(home) : std::string(".");
    return dir + "/" + leaf;
}

// Snapshot: <base>.bmp (screenshot, GuiManager) + <base>.dat (plot data); runs on the GUI thread.

static bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// dir/spike_YYYYmmdd_HHMMSS, suffixed _1,_2... until both files are free (shared suffix); empty if none free.
static std::string snapshot_base_path(const std::string& dir) {
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
    const std::string stem = dir + "/spike_" + ts;
    std::string base = stem;
    for (int i = 1; file_exists(base + ".bmp") || file_exists(base + ".dat"); ++i) {
        if (i > 999) return std::string();
        base = stem + "_" + std::to_string(i);
    }
    return base;
}

// .dat format: line-oriented text with at most one trailing binary blob. One
// section per VISIBLE plot with data:
//   [trigger]     n/pre_ms/post_ms/frame, then CSV time_ms,power_db (t=0 at trigger)
//   [spectrum]    n_fft, then CSV freq_hz,mag_db (baseband)
//   [spectrogram] rows/cols/row_seconds/freq_min_hz/freq_max_hz, then
//                 "BINARY <nbytes>" + raw little-endian float32 dB, row-major, newest row first
static bool write_snapshot_dat(const std::string& path,
                               bool want_trig, bool want_spec, bool want_sgram,
                               double rate_hz, double detection_rate_hz,
                               double freq_mhz,
                               Trig& trig, PlotCSpectrumBlock& spec,
                               PlotCSpectrogramBlock& sgram,
                               std::string& err) {
    // Copy out of the blocks first (cheap GUI-thread copy) so no lock is held while writing.
    std::vector<float> trig_y;
    float pre_ms = 0.0f, post_ms = 0.0f;
    size_t trig_idx = 0, trig_rate = 0;
    unsigned long frame = 0;
    bool have_trig = want_trig &&
                     trig.export_frame(trig_y, pre_ms, post_ms, trig_idx, frame,
                                       trig_rate);

    std::vector<float> sp_freq, sp_mag;
    bool have_spec = want_spec && spec.export_spectrum(0, sp_freq, sp_mag);

    std::vector<float> sg;
    size_t sg_rows = 0, sg_cols = 0, sg_fpr = 0, sg_sps = 0;
    bool have_sgram = want_sgram &&
                      sgram.export_display(0, sg, sg_rows, sg_cols, sg_fpr, sg_sps);

    if (!have_trig && !have_spec && !have_sgram) {
        err = "no visible plot has data yet";
        return false;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }

    char ts[64];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    f << "# spike snapshot " << ts << "\n"
      << "# format: text sections for the plots visible at snapshot time;\n"
      << "#   [trigger]     n/pre_ms/post_ms/frame, then CSV time_ms,power_db (trigger at t=0)\n"
      << "#   [spectrum]    n_fft, then CSV freq_hz,mag_db (baseband)\n"
      << "#   [spectrogram] rows/cols/row_seconds/freq range, then 'BINARY <nbytes>'\n"
      << "#                 + raw little-endian float32 dB, row-major, newest row first,\n"
      << "#                 at the very end of the file\n";
    char line[96];
    std::snprintf(line, sizeof(line), "# sample_rate_hz %.0f\n", rate_hz);
    f << line;
    // Zero-span stream rate (device/decimation); the [trigger] section's own axis uses the capture-time rate.
    std::snprintf(line, sizeof(line), "# detection_rate_hz %.0f\n", detection_rate_hz);
    f << line;
    std::snprintf(line, sizeof(line), "# center_freq_mhz %.6f\n", freq_mhz);
    f << line;

    if (have_trig) {
        // Use the CAPTURED rate (trigger snapshot metadata) -- current device rate may already differ.
        const double dt_ms = 1000.0 / static_cast<double>(trig_rate);
        f << "[trigger]\n"
          << "n " << trig_y.size() << "\n";
        std::snprintf(line, sizeof(line), "pre_ms %.6f\npost_ms %.6f\n",
                      static_cast<double>(pre_ms), static_cast<double>(post_ms));
        f << line;
        f << "frame " << frame << "\n"
          << "# columns: time_ms,power_db\n";
        for (size_t i = 0; i < trig_y.size(); ++i) {
            double tm = (static_cast<double>(i) - static_cast<double>(trig_idx)) * dt_ms;
            std::snprintf(line, sizeof(line), "%.4f,%.3f\n",
                          tm, static_cast<double>(trig_y[i]));
            f << line;
        }
    }

    if (have_spec) {
        f << "[spectrum]\n"
          << "n_fft " << sp_mag.size() << "\n"
          << "# columns: freq_hz,mag_db\n";
        for (size_t i = 0; i < sp_mag.size(); ++i) {
            std::snprintf(line, sizeof(line), "%.1f,%.3f\n",
                          static_cast<double>(sp_freq[i]),
                          static_cast<double>(sp_mag[i]));
            f << line;
        }
    }

    if (have_sgram) {
        const double row_seconds = (sg_sps > 0)
            ? static_cast<double>(sg_fpr) * static_cast<double>(sg_cols)
                  / static_cast<double>(sg_sps)
            : 0.0;
        f << "[spectrogram]\n"
          << "rows " << sg_rows << "\n"
          << "cols " << sg_cols << "\n";
        std::snprintf(line, sizeof(line), "row_seconds %.9f\n", row_seconds);
        f << line;
        std::snprintf(line, sizeof(line), "freq_min_hz %.1f\nfreq_max_hz %.1f\n",
                      -static_cast<double>(sg_sps) / 2.0,
                      static_cast<double>(sg_sps) / 2.0);
        f << line;
        f << "order newest_row_first\n"
          << "encoding binary_float32_le\n"
          << "BINARY " << sg.size() * sizeof(float) << "\n";
        f.write(reinterpret_cast<const char*>(sg.data()),
                static_cast<std::streamsize>(sg.size() * sizeof(float)));
    }

    f.flush();
    if (!f) {
        err = "write to " + path + " failed";
        return false;
    }
    return true;
}

// Builds the flowgraph around an already-constructed source block and runs the
// GUI until the window closes. Templated on the concrete source block type so
// one body serves both UHD and HackRF; all device access goes through the
// ISource adapter, except the flowgraph runner, which needs the concrete block.
// args.rate must already hold the ACTUAL device rate before this is called.
template<typename SourceBlock>
static int run_app(SourceBlock& source, ISource& src_if, SpikeArgs& args,
                   const char* window_title) {
    cler::GuiManager gui(1500, 900, window_title);

    // Persist window layout to a stable location (independent of working dir).
    ImGuiIO& io = ImGui::GetIO();
    static std::string imgui_ini = config_path(".cler_spike_imgui.ini");
    io.IniFilename = imgui_ini.c_str();

    FanoutBlock<std::complex<float>> fanout("Fanout", 4);
    PowerDetectorBlock<std::complex<float>> power("PowerDetector", -120.0f);

    Trig trigger("Trigger", static_cast<size_t>(args.rate),
                 /*threshold*/   -40.0f,
                 /*window_ms*/    20.0f,
                 /*pretrigger%*/  10.0f,
                 /*holdoff_ms*/   100.0f,
                 Trig::Edge::Rising,
                 Trig::Mode::Auto,
                 /*hysteresis*/   3.0f,
                 /*auto_ms*/      200.0f,
                 /*max_window_ms*/5000.0f);

    PlotCSpectrumBlock spectrum("Spectrum", {"I/Q"}, static_cast<size_t>(args.rate), args.fft);

    // Drains its whole input each call so it never stalls the shared fanout (min-space commit); paused via set_active() while hidden.
    const size_t waterfall_rows = 2000;
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"I/Q"},
        static_cast<size_t>(args.rate), args.fft, waterfall_rows);

    ChannelizerPanelBlock channelizer("Channelizer");

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    spectrogram.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    channelizer.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel(&src_if, &trigger, &spectrum, &spectrogram, &power,
                       &channelizer, waterfall_rows, args);
    const std::string settings_file = config_path(".cler_spike.conf");
    panel.load(settings_file);   // restore last session's settings if present

    // Trigger is a sink: renders its own capture (oscilloscope-style), no downstream channel.
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source,  &fanout.in),
        cler::BlockRunner(&fanout,  &power.in, &spectrum.in[0], &spectrogram.in[0],
                                    &channelizer.in),
        cler::BlockRunner(&power,   &trigger.in),
        cler::BlockRunner(&trigger),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&channelizer)
    );

    flowgraph.run();
    panel.apply_all();   // sync radio + trigger to loaded/initial settings
    // Report what the panel actually applied; args.* may be stale by now.
    std::cout << "CLER Spike running at " << panel.freq_mhz() << " MHz, "
              << panel.rate_hz() / 1e6 << " MS/s. Close window to exit." << std::endl;

    // Tile visible plots as equal-height rows right of the panel: on first frame, view-set change, "Arrange windows", or a debounced resize.
    bool prev_scope = false, prev_spectrum = false, prev_spectrogram = false;
    bool prev_channelizer = false;
    bool first_layout = true;
    // Retile only once WorkSize is STABLE for kResizeSettleFrames (no fighting a drag) and differs from the size last tiled.
    constexpr int kResizeSettleFrames = 5;
    ImVec2 tiled_work_size(0.0f, 0.0f);     // WorkSize used by the last tiling
    ImVec2 pending_work_size(0.0f, 0.0f);   // last observed WorkSize
    int    work_size_stable = 0;            // frames it has been unchanged

    while (!gui.should_close()) {
        // Hidden plots keep draining input (fanout never stalls) but skip FFT/copy work, so they can't jitter the trigger path.
        spectrogram.set_active(panel.show_spectrogram);
        spectrum.set_active(panel.show_spectrum);
        channelizer.set_active(panel.show_channelizer);

        gui.begin_frame();
        panel.render();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp->WorkSize.x != pending_work_size.x ||
            vp->WorkSize.y != pending_work_size.y) {
            pending_work_size = vp->WorkSize;
            work_size_stable  = 0;
        } else if (work_size_stable < kResizeSettleFrames) {
            ++work_size_stable;
        }
        const bool resize_retile =
            work_size_stable >= kResizeSettleFrames &&
            (vp->WorkSize.x != tiled_work_size.x ||
             vp->WorkSize.y != tiled_work_size.y);

        // Read visibility AFTER render() (toggle applies this frame) but stage rects BEFORE the view renders below consume them.
        bool retile = first_layout || panel.arrange_requested || resize_retile ||
                      panel.show_scope       != prev_scope ||
                      panel.show_spectrum    != prev_spectrum ||
                      panel.show_spectrogram != prev_spectrogram ||
                      panel.show_channelizer != prev_channelizer;
        first_layout = false;
        panel.arrange_requested = false;
        prev_scope       = panel.show_scope;
        prev_spectrum    = panel.show_spectrum;
        prev_spectrogram = panel.show_spectrogram;
        prev_channelizer = panel.show_channelizer;

        if (retile) {
            // Plot area = viewport minus the panel's rect and margins, split into N equal rows.
            tiled_work_size = vp->WorkSize;
            const float gap = 8.0f;
            const float x = panel.window_pos.x + panel.window_size.x + 10.0f;
            const float w = vp->WorkPos.x + vp->WorkSize.x - 10.0f - x;
            const float y0 = vp->WorkPos.y + 10.0f;
            const float total_h = vp->WorkSize.y - 20.0f;
            const int n = (panel.show_scope ? 1 : 0) +
                          (panel.show_spectrum ? 1 : 0) +
                          (panel.show_spectrogram ? 1 : 0) +
                          (panel.show_channelizer ? 1 : 0);
            if (n > 0 && w > 50.0f && total_h > 50.0f) {
                const float row_h = (total_h - gap * static_cast<float>(n - 1))
                                    / static_cast<float>(n);
                int row = 0;
                auto row_y = [&](int r) { return y0 + static_cast<float>(r) * (row_h + gap); };
                if (panel.show_scope)       trigger.apply_window_rect(x, row_y(row++), w, row_h);
                if (panel.show_spectrum)    spectrum.apply_window_rect(x, row_y(row++), w, row_h);
                if (panel.show_spectrogram) spectrogram.apply_window_rect(x, row_y(row++), w, row_h);
                if (panel.show_channelizer) channelizer.apply_window_rect(x, row_y(row++), w, row_h);
            }
        }

        if (panel.show_scope)       trigger.render();
        if (panel.show_spectrum)    spectrum.render();
        if (panel.show_spectrogram) spectrogram.render();
        if (panel.show_channelizer) channelizer.render();

        // Consumed after render() (exported data matches this frame) and before end_frame()'s screenshot pass.
        if (panel.snapshot_requested) {
            panel.snapshot_requested = false;
            std::string base = snapshot_base_path(panel.snapshot_dir());
            if (base.empty()) {
                panel.set_status("Snapshot failed: no free filename in " +
                                 panel.snapshot_dir());
            } else {
                std::string err;
                bool dat_ok = write_snapshot_dat(
                    base + ".dat",
                    panel.show_scope, panel.show_spectrum, panel.show_spectrogram,
                    panel.rate_hz(),
                    static_cast<double>(panel.detection_rate()),
                    static_cast<double>(panel.freq_mhz()),
                    trigger, spectrum, spectrogram, err);
                gui.request_screenshot(base + ".bmp");   // written in end_frame()
                // Absolute path so files are easy to locate even if snapshot dir was relative.
                std::string full =
                    std::filesystem::absolute(base).lexically_normal().string();
                if (dat_ok) {
                    panel.set_status("Saved " + full + ".bmp/.dat");
                } else {
                    panel.set_status("Saved " + full + ".bmp; .dat failed: " + err);
                }
            }
        }

        gui.end_frame();
        // Vsync already paces this loop; the tiny sleep just keeps CPU low if vsync is off/bypassed.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    flowgraph.stop();
    panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << src_if.get_overflow_count() << std::endl;
    return 0;
}

int main(int argc, char** argv) {
    SpikeArgs args = parse_args(argc, argv);

    if (args.source == SourceKind::HackRF) {
        // HackRF takes integer freq/rate; no live rate change, so the
        // constructed rate is the actual rate for everything downstream.
        SourceHackRFBlock source("HackRF",
            static_cast<uint64_t>(args.freq + 0.5),
            static_cast<uint32_t>(args.rate + 0.5),
            args.lna, args.vga, args.amp);
        args.rate = static_cast<double>(source.get_sample_rate());
        HackRFSourceAdapter adapter(&source);
        return run_app(source, adapter, args, "CLER Spike - HackRF");
    }

    SourceUHDBlock<std::complex<float>> source("USRP", args.freq, args.rate,
        args.device_address, args.gain, 1, "sc16", /*quiet=*/true);
    // Hardware may snap the requested rate; everything rate-derived uses the ACTUAL rate.
    args.rate = source.actual_sample_rate();
    UHDSourceAdapter adapter(&source);
    return run_app(source, adapter, args, "CLER Spike - USRP");
}
