// Slim "Spike-like" spectrum analyzer GUI for USRP on CLER.
//
// Reliable zero-span (power-vs-time) capture with a real trigger, plus spectrum
// and spectrogram (waterfall) views, all driven from one live control panel.
//
//   USRP --> Fanout(3) --+--> PowerDetector --> Trigger --> PlotTimeSeries (zero-span)
//                        +--> PlotCSpectrum                  (spectrum)
//                        +--> PlotCSpectrogram               (waterfall)
//
// Single superset flowgraph: every block always runs; the "View" checkboxes only
// choose which windows are drawn. Center frequency, gain and sample rate (span)
// are all live; a rate change is staged into the source's streaming thread and
// every rate-derived consumer re-syncs when the ACTUAL hardware rate lands
// (see ControlPanel::on_rate_changed).

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/triggers/trigger_block.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "power_detector.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using Trig = TriggerBlock<float>;
using PowerDet = PowerDetectorBlock<std::complex<float>>;

// Small "(?)" hover help, like ImGui's demo HelpMarker.
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
    double freq = 915e6;
    double rate = 1e6;
    double gain = 30.0;
    size_t fft  = 2048;
    bool   rate_from_cli = false;   // -r given explicitly: overrides saved rate_hz
    std::string device_address;
};

static void print_usage(const char* prog) {
    std::cout << "\nSlim Spike-like analyzer for USRP\n"
              << "Usage: " << prog << " [OPTIONS]\n"
              << "  -f, --freq FREQ   Center frequency Hz (default 915e6)\n"
              << "  -r, --rate RATE   Initial sample rate S/s (default 1e6; live-tunable\n"
              << "                    in the GUI; if given, overrides the saved rate)\n"
              << "  -g, --gain GAIN   Gain dB (default 30)\n"
              << "  -F, --fft  SIZE   FFT size for spectrum view (default 2048)\n"
              << "  -d, --dev  ADDR   USRP device address (default auto)\n"
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
        else if (arg == "-f" || arg == "--freq") a.freq = std::stod(next());
        else if (arg == "-r" || arg == "--rate") { a.rate = std::stod(next()); a.rate_from_cli = true; }
        else if (arg == "-g" || arg == "--gain") a.gain = std::stod(next());
        else if (arg == "-F" || arg == "--fft")  a.fft  = std::stoul(next());
        else if (arg == "-d" || arg == "--dev" || arg == "--device") a.device_address = next();
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); exit(1); }
    }
    return a;
}

// Render-only control surface. Owns no DSP; it reads/writes shared state on the
// source (live retune, staged into the streaming thread) and the trigger
// (mutex-guarded config snapshot applied at a safe point).
struct ControlPanel {
    ControlPanel(SourceUHDBlock<std::complex<float>>* src, Trig* trig,
                 PlotCSpectrumBlock* spectrum,
                 PlotCSpectrogramBlock* sgram,
                 PowerDetectorBlock<std::complex<float>>* power,
                 size_t sgram_tall, const SpikeArgs& a)
        : _src(src), _trig(trig), _spectrum(spectrum), _sgram(sgram), _power(power),
          _freq_mhz(static_cast<float>(a.freq / 1e6)),
          _freq_anchor_mhz(_freq_mhz),
          _gain_db(static_cast<float>(a.gain)),
          _rate_hz(a.rate),
          _rate_msps(static_cast<float>(a.rate / 1e6)),
          _rate_from_cli(a.rate_from_cli),
          _n_fft(a.fft),
          _sgram_tall(sgram_tall),
          _zs_bw_mhz(static_cast<float>(a.rate / 1e6)),   // full rate = bypass (old behavior)
          _max_window_ms(trig->max_window_ms()) {
        _history_s = fpr_to_history(8);   // default depth; conf key history_s overrides
    }

    // A slider (or drag) that turns into a typed input box on double-click.
    // Returns true on the frames the value changed.
    // [vmin, vmax] bounds the slider/drag; the optional [tmin, tmax] bounds the
    // TYPED value instead (defaults to vmin/vmax when NaN). This lets a control
    // show a narrow slider window while still accepting any value by typing
    // (used by the center-frequency slider below).
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

    void render() {
        // Rate-change choke point, polled every frame (cheap atomic read). The
        // source applies a requested rate asynchronously on its streaming
        // thread, and the hardware may snap it -- so instead of assuming, we
        // watch the ACTUAL rate and re-sync every rate-derived consumer the
        // moment it changes. This one path covers typed changes, the saved
        // conf rate staged at startup, and any initial hardware snap alike.
        {
            const double actual = _src->actual_sample_rate();
            if (std::abs(actual - _rate_hz) > 0.5) on_rate_changed(actual);
        }

        ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Control");

        ImGui::TextUnformatted("View");
        ImGui::Separator();
        ImGui::Checkbox("Zero-span scope", &show_scope);
        ImGui::SameLine();
        ImGui::Checkbox("Spectrum", &show_spectrum);
        ImGui::Checkbox("Spectrogram (waterfall)", &show_spectrogram);
        help("Which windows to display. All views run continuously; these only "
             "toggle visibility, so nothing needs restarting.");

        if (ImGui::Button("Arrange windows")) arrange_requested = true;
        help("Re-tile the visible plot windows into equal-height rows next to "
             "the control panel. Also happens automatically when the View "
             "checkboxes change. You can still move/resize windows afterward.");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Radio");
        ImGui::Separator();

        // Sample rate (span), live. Number entry (double-click to type an
        // exact value); committed on edit-end like freq/gain. The new rate is
        // only REQUESTED here -- the source applies it on its streaming
        // thread, and the poll above re-syncs everything (including this
        // widget's value) when the actual rate lands.
        ImGui::SetNextItemWidth(180);
        editable("Rate (MS/s)", &_rate_msps, 0.1f, 61.44f, "%.3f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        if (ImGui::IsItemDeactivatedAfterEdit())
            push_rate_config(static_cast<double>(_rate_msps) * 1e6);
        help("Sample rate = displayed span, applied live. The hardware may "
             "snap the request to what its clocking supports; the shown value "
             "follows the ACTUAL device rate. Note: a capture straddling the "
             "switch may be garbled once, and the waterfall restarts.");

        // Apply freq/gain only when the user finishes editing, not every tick,
        // so we don't spam the USRP with retunes. Center frequency is a slider
        // over a narrow window (anchor +/- 25 MHz) since a full 0-6 GHz slider is
        // far too coarse; double-click to type ANY value in [0, 6000] MHz. The
        // anchor (and thus the slider range) is recomputed ONLY when an edit
        // commits -- recentering mid-drag would move the value->pixel mapping
        // under the cursor and feed back on itself.
        const float freq_lo = std::max(0.0f,    _freq_anchor_mhz - 25.0f);
        const float freq_hi = std::min(6000.0f, _freq_anchor_mhz + 25.0f);
        ImGui::SetNextItemWidth(180);
        editable("Center (MHz)", &_freq_mhz, freq_lo, freq_hi, "%.3f",
                 /*slider=*/true, 0.1f, /*tmin=*/0.0f, /*tmax=*/6000.0f);
        bool freq_done = ImGui::IsItemDeactivatedAfterEdit();
        if (freq_done) _freq_anchor_mhz = _freq_mhz;   // recenter for next time
        ImGui::SetNextItemWidth(180);
        editable("Gain (dB)", &_gain_db, 0.0f, 76.0f, "%.1f", /*slider=*/true);
        bool gain_done = ImGui::IsItemDeactivatedAfterEdit();
        if (freq_done || gain_done) push_radio_config();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Trigger");
        ImGui::Separator();

        // All numeric fields: drag the slider to adjust, or double-click to type.
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
        // Capture memory is fixed at startup, so the reachable window shrinks
        // at higher sample rates; refreshed by on_rate_changed().
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

        // Zero-span channel bandwidth: a decimating channel selector ahead of
        // the power detector. Applied on edit-commit like freq/gain (typed
        // exact values matter more than dragging here, hence a drag box). A
        // commit is treated exactly like a rate change for the trigger path:
        // the trigger runs at the DECIMATED rate, so push the decimator config
        // FIRST, then re-push the trigger with the new detection rate.
        ImGui::SetNextItemWidth(180);
        editable("Zero-span BW (MHz)", &_zs_bw_mhz, min_zs_bw_mhz(),
                 static_cast<float>(_rate_hz / 1e6), "%.4f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            clamp_zs_bw();
            push_power_config();
            refresh_trigger_window();
            push_trigger_config();
        }
        ImGui::SameLine();
        {
            const size_t R = PowerDet::decimation_for(zs_bw_hz(), _rate_hz);
            if (R <= 1) {
                ImGui::TextDisabled("(bypass)");
            } else {
                const double eff = _rate_hz / static_cast<double>(R);
                if (eff < 1e6) ImGui::TextDisabled("eff. %.1f kHz", eff / 1e3);
                else           ImGui::TextDisabled("eff. %.3f MHz", eff / 1e6);
            }
        }
        help("Channel (detection) bandwidth of the zero-span power trace: the "
             "I/Q stream is decimated by an integer factor R with an 80 dB "
             "anti-alias lowpass, so exactly this two-sided width around the "
             "center survives and off-channel bursts are rejected before the "
             "power is computed. The achieved value snaps to sample_rate/R -- "
             "'eff.' shows what is actually applied. Set to the full sample "
             "rate (default) to bypass. The trigger then runs at the decimated "
             "rate, so narrower bandwidths allow LONGER capture windows. A "
             "capture straddling a bandwidth change may be garbled once.");

        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Arm / Re-arm")) _trig->rearm();
        ImGui::SameLine();
        if (ImGui::Button("Force"))        _trig->force_trigger();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("State: %s", state_str(_trig->state()));

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
        // First-use modal: ask for the snapshot directory, then never again.
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

    // The rate of the power stream the TRIGGER sees: device rate / decimation.
    // All trigger-side ms<->samples math must use this, not the device rate.
    size_t detection_rate() const {
        const size_t R = PowerDet::decimation_for(zs_bw_hz(), _rate_hz);
        double det = std::round(_rate_hz / static_cast<double>(R));
        if (det < 1.0) det = 1.0;
        return static_cast<size_t>(det);
    }

    // Push current panel state to the radio, the trigger and the spectrogram
    // (used at startup after loading saved settings).
    void apply_all() {
        push_radio_config();
        // Decimator before trigger: the trigger's time base is the DECIMATED
        // rate, so the detection rate must be established (and the saved
        // window clamp run against it -- max window in ms GROWS at decimated
        // rates) before the trigger config is derived.
        clamp_zs_bw();
        push_power_config();
        refresh_trigger_window();
        push_trigger_config();
        // Waterfall depth: seed the spectrogram's frames-per-row once from the
        // saved (or default) seconds value. From here on the depth is owned by
        // the History slider in the spectrogram window itself; the panel only
        // reads it back at save() time.
        _sgram->set_frames_per_row(history_to_fpr(_history_s));
        // Saved sample rate (conf key rate_hz): staged LAST through the same
        // live path as a typed change, so it lands as the newest pending
        // config (request_configure keeps only the latest) and the per-frame
        // poll runs on_rate_changed() once the streaming thread applies it.
        // An explicit CLI -r wins over the saved value.
        if (!_rate_from_cli && _loaded_rate_hz > 0.0 &&
            std::abs(_loaded_rate_hz - _rate_hz) > 0.5) {
            push_rate_config(_loaded_rate_hz);
        }
    }

    // Which windows to draw (read by the main render loop). Public so main() can
    // gate the render() calls; toggled by the "View" checkboxes above.
    bool show_scope       = true;
    bool show_spectrum    = true;
    bool show_spectrogram = false;

    // One-shot request from the "Arrange windows" button; consumed (cleared)
    // by the main loop, which owns the tiling computation.
    bool arrange_requested = false;

    // One-shot request from the "Snapshot" button; consumed by the main loop,
    // which has the blocks and the GuiManager in scope.
    bool snapshot_requested = false;

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string key;
        while (f >> key) {
            if      (key == "freq_mhz")       f >> _freq_mhz;
            else if (key == "gain_db")        f >> _gain_db;
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
            else if (key == "history_s")      f >> _history_s;
            else if (key == "zs_bw_hz") {
                // Stored in Hz for backward compatibility; the widget is MHz.
                float bw_hz = 0.0f;
                f >> bw_hz;
                _zs_bw_mhz = bw_hz / 1e6f;
            }
            else if (key == "rate_hz")        f >> _loaded_rate_hz;
            else if (key == "snapshot_dir") {
                // Value is the REST OF THE LINE (may contain spaces), not one
                // whitespace-delimited token like every other key.
                std::string rest;
                std::getline(f, rest);
                _snapshot_dir = trimmed(rest.c_str());
            }
            else { std::string skip; std::getline(f, skip); }
        }
        // A saved bandwidth may exceed this session's sample rate (or sit
        // below the achievable floor). NOTE: the saved WINDOW is deliberately
        // NOT clamped here -- its limit depends on the DECIMATED rate, which
        // is only derivable once the bandwidth is applied; apply_all() (and
        // every on_rate_changed) runs the clamp at the right time.
        clamp_zs_bw();
        _freq_anchor_mhz = _freq_mhz;   // center the freq slider on the saved value
        return true;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "freq_mhz "       << _freq_mhz       << "\n"
          << "gain_db "        << _gain_db        << "\n"
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
          << "zs_bw_hz "         << static_cast<double>(_zs_bw_mhz) * 1e6 << "\n"
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

    void push_radio_config() {
        UHDConfig cfg;
        cfg.center_freq_Hz = static_cast<double>(_freq_mhz) * 1e6;
        cfg.sample_rate_Hz = _rate_hz;   // current rate: the source skips this no-op
        cfg.gain           = static_cast<double>(_gain_db);
        cfg.bandwidth_Hz   = _rate_hz;
        _src->request_configure(cfg);
    }

    // Stage a NEW sample rate into the source (freq/gain carried unchanged;
    // analog bandwidth tracks the rate). Applied asynchronously on the
    // source's streaming thread; the poll in render() picks up the resulting
    // actual rate and runs on_rate_changed().
    void push_rate_config(double rate_hz) {
        UHDConfig cfg;
        cfg.center_freq_Hz = static_cast<double>(_freq_mhz) * 1e6;
        cfg.sample_rate_Hz = rate_hz;
        cfg.gain           = static_cast<double>(_gain_db);
        cfg.bandwidth_Hz   = rate_hz;
        _src->request_configure(cfg);
    }

    // Single choke point for a sample-rate change: runs when the ACTUAL
    // device rate differs from what the panel last synced to. Re-derives
    // every rate-dependent piece of state in one place.
    void on_rate_changed(double actual_hz) {
        _rate_hz   = actual_hz;
        _rate_msps = static_cast<float>(actual_hz / 1e6);   // widget display

        // Zero-span channel bandwidth: keep the REQUESTED value, re-clamped
        // to the new rate's achievable range; the decimation factor R is
        // re-derived from it. Push the decimator FIRST -- the trigger's time
        // base below is the DECIMATED rate.
        clamp_zs_bw();
        push_power_config();

        // Trigger: capture memory never reallocates, so the max window in ms
        // shrinks at higher detection rates (and grows at narrow bandwidths).
        // Re-clamp, then push ONE config carrying the new detection rate
        // (sample counts are derived from it inside set_config, so the rate
        // and its counts land on the block thread as a single generation).
        refresh_trigger_window();
        push_trigger_config();

        // Frequency axes of both FFT views (GUI-thread setters; the
        // spectrogram also clears its ring -- old rows would be mislabeled).
        const size_t sps = static_cast<size_t>(actual_hz + 0.5);
        _spectrum->set_sample_rate(sps);
        _sgram->set_sample_rate(sps);
        // The spectrogram's own History slider (seconds) rescales itself from
        // the new rate; frames-per-row is left as-is on purpose.
    }

    // History depth (s) <-> spectrogram frames/row, used only to translate the
    // conf key `history_s` at load/save time (the live control is the History
    // slider inside the spectrogram window). One waterfall row spans
    // fpr * n_fft / sps seconds and the ring holds `tall` rows.
    size_t history_to_fpr(float history_s) const {
        double fpr = std::round(static_cast<double>(history_s) * _rate_hz
                                / (static_cast<double>(_n_fft)
                                   * static_cast<double>(_sgram_tall)));
        if (fpr < 1.0)   fpr = 1.0;
        if (fpr > 256.0) fpr = 256.0;
        return static_cast<size_t>(fpr);
    }
    float fpr_to_history(size_t fpr) const {
        return static_cast<float>(static_cast<double>(fpr)
                                  * static_cast<double>(_n_fft)
                                  * static_cast<double>(_sgram_tall) / _rate_hz);
    }

    // Requested zero-span bandwidth in Hz (the widget shows MHz).
    double zs_bw_hz() const { return static_cast<double>(_zs_bw_mhz) * 1e6; }

    // Achievable bandwidth floor at the current rate: the decimation factor
    // is capped (rate/Rmax), so requests below this would silently snap 10x
    // away from what was typed -- clamp the widget instead.
    float min_zs_bw_mhz() const {
        const double floor_hz = std::max(PowerDet::MIN_OUTPUT_RATE_HZ,
                                         _rate_hz / static_cast<double>(PowerDet::MAX_DECIMATION));
        return static_cast<float>(std::min(floor_hz, _rate_hz) / 1e6);
    }

    void clamp_zs_bw() {
        const float hi = static_cast<float>(_rate_hz / 1e6);
        _zs_bw_mhz = std::min(std::max(_zs_bw_mhz, min_zs_bw_mhz()), hi);
    }

    // Re-derive the reachable capture window (fixed sample capacity expressed
    // in ms at the current DETECTION rate) and re-clamp the widget.
    void refresh_trigger_window() {
        _max_window_ms = 1000.0f * static_cast<float>(_trig->max_window_samples())
                       / static_cast<float>(detection_rate());
        if (_window_ms > _max_window_ms) _window_ms = _max_window_ms;
    }

    void push_power_config() {
        _power->set_channel_bandwidth(zs_bw_hz(), _rate_hz);
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

    SourceUHDBlock<std::complex<float>>* _src;
    Trig* _trig;
    PlotCSpectrumBlock* _spectrum;
    PlotCSpectrogramBlock* _sgram;
    PowerDetectorBlock<std::complex<float>>* _power;

    float  _freq_mhz;
    // Center of the freq slider's +/- 25 MHz window; moved only on commit (see
    // render()). Re-seeded after load() so it starts on the saved frequency.
    float  _freq_anchor_mhz;
    float  _gain_db;
    double _rate_hz;                // the ACTUAL device rate last synced to
    float  _rate_msps;              // rate widget value (MS/s), follows _rate_hz
    double _loaded_rate_hz = 0.0;   // conf key rate_hz (0 = not present)
    bool   _rate_from_cli;          // -r given: CLI wins over the saved rate
    size_t _n_fft;
    size_t _sgram_tall;
    float  _history_s = 0.0f;   // waterfall depth (s) from conf/default; only
                                // used to seed the spectrogram in apply_all()
    float  _zs_bw_mhz;          // zero-span channel bandwidth (MHz); rate = bypass

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

    // Snapshot: destination directory (conf key snapshot_dir; asked for once
    // via the modal above), the modal's edit buffer, and the transient status.
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

// ---------------------------------------------------------------------------
// Snapshot support: <base>.bmp (screenshot, written by GuiManager) and
// <base>.dat (plot data). All of this runs on the GUI thread; allocation and
// file IO are fine here.

static bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// dir/spike_YYYYmmdd_HHMMSS, suffixed _1, _2, ... until BOTH <base>.bmp and
// <base>.dat are free (the two files always share the suffix). Empty string
// if nothing is free (pathological).
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

// Snapshot data file (.dat) format -- self-describing; this description is
// also written into the file header itself. Line-oriented text, '\n' endings,
// with at most one raw binary blob at the very END of the file:
//   '#' lines: comments/global metadata ("# spike snapshot <local time>",
//   "# sample_rate_hz N", "# center_freq_mhz F", plus this format doc).
//   Then one section per plot that was VISIBLE and had data when the snapshot
//   was taken (a hidden plot gets no section):
//   [trigger]     metadata lines "n <samples>", "pre_ms <f>", "post_ms <f>",
//                 "frame <trigger frame counter>", then n CSV lines
//                 "time_ms,power_db" -- time axis reconstructed exactly as the
//                 scope renders it: trigger instant at t=0, pre-trigger
//                 samples negative.
//   [spectrum]    metadata "n_fft <bins>", then n_fft CSV lines
//                 "freq_hz,mag_db" (baseband Hz; the averaged display trace).
//   [spectrogram] metadata "rows <r>", "cols <c>", "row_seconds <s>" (time
//                 span of one row = frames_per_row * n_fft / sps),
//                 "freq_min_hz", "freq_max_hz" (baseband),
//                 "order newest_row_first", "encoding binary_float32_le",
//                 then "BINARY <nbytes>" followed by exactly that many raw
//                 bytes: rows*cols little-endian float32 dB values, row-major
//                 (binary on purpose -- ~16 MB as text would hang the GUI).
//                 Nothing follows the blob.
static bool write_snapshot_dat(const std::string& path,
                               bool want_trig, bool want_spec, bool want_sgram,
                               double rate_hz, double detection_rate_hz,
                               double freq_mhz,
                               Trig& trig, PlotCSpectrumBlock& spec,
                               PlotCSpectrogramBlock& sgram,
                               std::string& err) {
    // Copy everything out of the blocks FIRST (cheap GUI-thread copies) so no
    // block state or lock is held while the file is written.
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
    // Rate of the zero-span power/trigger stream (device rate / decimation);
    // the [trigger] section's own time axis uses the capture-time rate.
    std::snprintf(line, sizeof(line), "# detection_rate_hz %.0f\n", detection_rate_hz);
    f << line;
    std::snprintf(line, sizeof(line), "# center_freq_mhz %.6f\n", freq_mhz);
    f << line;

    if (have_trig) {
        // Use the rate the frame was CAPTURED at (part of the trigger's
        // snapshot metadata) -- the current device rate may already differ.
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

int main(int argc, char** argv) {
    SpikeArgs args = parse_args(argc, argv);

    // Probe the device once so a failure is reported cleanly before we build
    // the rest of the graph (mirrors the pattern in uhd_device.cpp).
    try {
        SourceUHDBlock<std::complex<float>> probe("USRP", args.freq, args.rate,
            args.device_address, args.gain, 1);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize USRP: " << e.what() << std::endl;
        return 1;
    }

    SourceUHDBlock<std::complex<float>> usrp("USRP", args.freq, args.rate,
        args.device_address, args.gain, 1);

    // The hardware may snap the requested rate (e.g. B2xx clock dividers);
    // everything rate-derived below (trigger timing, FFT axes, panel math)
    // must be built from the ACTUAL rate, not the request -- previously the
    // requested args.rate was used, silently mis-scaling all axes on a snap.
    args.rate = usrp.actual_sample_rate();

    cler::GuiManager gui(1500, 900, "CLER Spike - USRP");

    // Persist window layout to a stable location (independent of working dir).
    ImGuiIO& io = ImGui::GetIO();
    static std::string imgui_ini = config_path(".cler_spike_imgui.ini");
    io.IniFilename = imgui_ini.c_str();

    FanoutBlock<std::complex<float>> fanout("Fanout", 3);
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

    // Waterfall: 2000 rows of history, each row peak-holding several FFT frames
    // (adjustable live via "frames/row" in the window). Drains its whole input
    // each call so it never stalls the shared fanout (which commits the min
    // space across all branches). When its window is hidden it is paused (see
    // set_active in the loop) so it costs nothing and can't perturb the trigger.
    const size_t waterfall_tall = 2000;   // rows of waterfall history
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"I/Q"},
        static_cast<size_t>(args.rate), args.fft, waterfall_tall);
    // History depth (frames/row) is owned by the History slider inside the
    // spectrogram window; the panel only seeds it once (default ~8 frames/row,
    // overridden by the `history_s` conf key) via panel.apply_all() and reads
    // it back at save() time.

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    spectrogram.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel(&usrp, &trigger, &spectrum, &spectrogram, &power,
                       waterfall_tall, args);
    const std::string settings_file = config_path(".cler_spike.conf");
    panel.load(settings_file);   // restore last session's settings if present

    // Trigger is a sink: it consumes the power stream and renders the captured
    // window itself (oscilloscope style), so it has no downstream channel.
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp,    &fanout.in),
        cler::BlockRunner(&fanout,  &power.in, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&power,   &trigger.in),
        cler::BlockRunner(&trigger),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram)
    );

    flowgraph.run();
    panel.apply_all();   // sync radio + trigger to loaded/initial settings
    std::cout << "CLER Spike running at " << args.freq / 1e6 << " MHz, "
              << args.rate / 1e6 << " MS/s. Close window to exit." << std::endl;

    // Auto-layout: on the first frame, whenever the set of visible views
    // changes, or on the panel's "Arrange windows" button, tile the visible
    // plot windows as equal-height rows in the area right of the control
    // panel. Each block applies its rect exactly once (apply_window_rect), so
    // the user can still move/resize freely afterward.
    bool prev_scope = false, prev_spectrum = false, prev_spectrogram = false;
    bool first_layout = true;

    while (!gui.should_close()) {
        // Pause the spectrogram/spectrum whenever they aren't shown: they keep
        // draining their input (so the fanout never stalls) but do no FFT or
        // copy work and cannot steal cycles from or add jitter to the trigger
        // path. The trigger itself has no equivalent: its procedure() IS the
        // trigger engine and must keep running even when the scope is hidden.
        spectrogram.set_active(panel.show_spectrogram);
        spectrum.set_active(panel.show_spectrum);

        gui.begin_frame();
        panel.render();

        // Detect visibility changes AFTER panel.render() (so a checkbox toggle
        // takes effect this frame) but stage the rects BEFORE the view renders
        // below consume them.
        bool retile = first_layout || panel.arrange_requested ||
                      panel.show_scope       != prev_scope ||
                      panel.show_spectrum    != prev_spectrum ||
                      panel.show_spectrogram != prev_spectrogram;
        first_layout = false;
        panel.arrange_requested = false;
        prev_scope       = panel.show_scope;
        prev_spectrum    = panel.show_spectrum;
        prev_spectrogram = panel.show_spectrogram;

        if (retile) {
            // Plot area: main viewport minus the control panel strip on the
            // left (panel sits in x < 370; plots start at +380) and 10 px
            // outer margins, split into N equal-height rows with small gaps.
            // Note: a hidden view keeps its pending rect until it is next
            // shown (its render() isn't called), which is what we want.
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float gap = 8.0f;
            const float x = vp->WorkPos.x + 380.0f;
            const float w = vp->WorkPos.x + vp->WorkSize.x - 10.0f - x;
            const float y0 = vp->WorkPos.y + 10.0f;
            const float total_h = vp->WorkSize.y - 20.0f;
            const int n = (panel.show_scope ? 1 : 0) +
                          (panel.show_spectrum ? 1 : 0) +
                          (panel.show_spectrogram ? 1 : 0);
            if (n > 0 && w > 50.0f && total_h > 50.0f) {
                const float row_h = (total_h - gap * static_cast<float>(n - 1))
                                    / static_cast<float>(n);
                int row = 0;
                auto row_y = [&](int r) { return y0 + static_cast<float>(r) * (row_h + gap); };
                if (panel.show_scope)       trigger.apply_window_rect(x, row_y(row++), w, row_h);
                if (panel.show_spectrum)    spectrum.apply_window_rect(x, row_y(row++), w, row_h);
                if (panel.show_spectrogram) spectrogram.apply_window_rect(x, row_y(row++), w, row_h);
            }
        }

        if (panel.show_scope)       trigger.render();
        if (panel.show_spectrum)    spectrum.render();
        if (panel.show_spectrogram) spectrogram.render();

        // Snapshot: consumed here (after the plot renders, so the exported
        // data matches this frame) and before end_frame(), whose screenshot
        // pass captures the frame being drawn right now.
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
                std::string leaf = base.substr(base.find_last_of('/') + 1);
                if (dat_ok) {
                    panel.set_status("Saved " + leaf + ".bmp/.dat");
                } else {
                    panel.set_status("Saved " + leaf + ".bmp; .dat failed: " + err);
                }
            }
        }

        gui.end_frame();
        // Vsync (glfwSwapInterval(1) in GuiManager) already paces this loop at
        // the monitor's refresh rate; the extra 16 ms sleep on top of it
        // capped the UI at ~30 fps. Keep a tiny sleep so CPU stays low if
        // vsync is off or bypassed by the compositor, without halving the
        // frame rate.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    flowgraph.stop();
    panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << usrp.get_overflow_count() << std::endl;
    return 0;
}
