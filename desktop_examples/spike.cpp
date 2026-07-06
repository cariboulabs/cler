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
// choose which windows are drawn. Sample rate (span) is fixed at startup; center
// frequency and gain are live.

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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using Trig = TriggerBlock<float>;

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
    std::string device_address;
};

static void print_usage(const char* prog) {
    std::cout << "\nSlim Spike-like analyzer for USRP\n"
              << "Usage: " << prog << " [OPTIONS]\n"
              << "  -f, --freq FREQ   Center frequency Hz (default 915e6)\n"
              << "  -r, --rate RATE   Sample rate S/s, fixed at startup (default 1e6)\n"
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
        else if (arg == "-r" || arg == "--rate") a.rate = std::stod(next());
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
                 PlotCSpectrogramBlock* sgram, size_t sgram_tall,
                 const SpikeArgs& a)
        : _src(src), _trig(trig), _sgram(sgram),
          _freq_mhz(static_cast<float>(a.freq / 1e6)),
          _freq_anchor_mhz(_freq_mhz),
          _gain_db(static_cast<float>(a.gain)),
          _rate_hz(a.rate),
          _n_fft(a.fft),
          _sgram_tall(sgram_tall),
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

        // Waterfall depth in seconds. Mapped onto the spectrogram's frames/row
        // (how many FFT frames are peak-held per row, clamped to [1, 256]), so
        // the achieved depth is quantized -- show what was actually applied.
        ImGui::SetNextItemWidth(120);
        if (editable("History (s)", &_history_s,
                     fpr_to_history(1), fpr_to_history(256), "%.1f",
                     /*slider=*/false, /*drag_speed=*/1.0f)) {
            push_spectrogram_config();
        }
        ImGui::SameLine();
        ImGui::Text("(actual %.1f s)", fpr_to_history(history_to_fpr(_history_s)));
        help("Total time span of the waterfall. Larger values peak-hold more FFT "
             "frames into each row, so long histories keep catching short bursts "
             "but with coarser time resolution per row.");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Radio");
        ImGui::Separator();
        ImGui::Text("Sample rate (span): %.3f MS/s  [fixed]", _rate_hz / 1e6);

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
        changed |= editable("Window (ms)", &_window_ms, 1.0f, _max_window_ms, "%.1f", true);
        help("Total time span captured and displayed per trigger (pre + post). "
             "This is the scope timebase. To see a whole repeating burst locked in "
             "place, set Window a bit LESS than the burst period and >= the burst span.");
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

        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Arm / Re-arm")) _trig->rearm();
        ImGui::SameLine();
        if (ImGui::Button("Force"))        _trig->force_trigger();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("State: %s", state_str(_trig->state()));

        ImGui::End();
    }

    // Push current panel state to the radio, the trigger and the spectrogram
    // (used at startup after loading saved settings).
    void apply_all() {
        push_radio_config();
        push_trigger_config();
        push_spectrogram_config();
    }

    // Which windows to draw (read by the main render loop). Public so main() can
    // gate the render() calls; toggled by the "View" checkboxes above.
    bool show_scope       = true;
    bool show_spectrum    = true;
    bool show_spectrogram = false;

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
            else { std::string skip; std::getline(f, skip); }
        }
        // A saved window may exceed this session's allocated max (different rate).
        if (_window_ms > _max_window_ms) _window_ms = _max_window_ms;
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
          << "history_s "        << _history_s       << "\n";
    }

private:
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
        cfg.sample_rate_Hz = _rate_hz;          // unchanged: span is fixed
        cfg.gain           = static_cast<double>(_gain_db);
        cfg.bandwidth_Hz   = _rate_hz;
        _src->request_configure(cfg);
    }

    // History depth (s) <-> spectrogram frames/row. One waterfall row spans
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

    void push_spectrogram_config() {
        _sgram->set_frames_per_row(history_to_fpr(_history_s));
    }

    void push_trigger_config() {
        _trig->set_config(_threshold_db, _window_ms, _pretrigger_pct,
                          _holdoff_ms,
                          _edge_idx == 0 ? Trig::Edge::Rising : Trig::Edge::Falling,
                          _mode_idx == 0 ? Trig::Mode::Normal
                                         : (_mode_idx == 1 ? Trig::Mode::Single
                                                           : Trig::Mode::Auto),
                          _hysteresis_db, _auto_ms);
    }

    SourceUHDBlock<std::complex<float>>* _src;
    Trig* _trig;
    PlotCSpectrogramBlock* _sgram;

    float  _freq_mhz;
    // Center of the freq slider's +/- 25 MHz window; moved only on commit (see
    // render()). Re-seeded after load() so it starts on the saved frequency.
    float  _freq_anchor_mhz;
    float  _gain_db;
    double _rate_hz;
    size_t _n_fft;
    size_t _sgram_tall;
    float  _history_s = 0.0f;   // waterfall depth in seconds (set in ctor)

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
};

static std::string config_path(const char* leaf) {
    const char* home = std::getenv("HOME");
    std::string dir = home ? std::string(home) : std::string(".");
    return dir + "/" + leaf;
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
    // History depth (frames/row) is owned by the panel: default ~8 frames/row,
    // overridden by the `history_s` conf key, applied via panel.apply_all().

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    spectrogram.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel(&usrp, &trigger, &spectrogram, waterfall_tall, args);
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

    while (!gui.should_close()) {
        // Pause the spectrogram whenever it isn't shown: it keeps draining its
        // input (so the fanout never stalls) but does no FFT work and cannot
        // steal cycles from or add jitter to the trigger path.
        spectrogram.set_active(panel.show_spectrogram);

        gui.begin_frame();
        panel.render();
        if (panel.show_scope)       trigger.render();
        if (panel.show_spectrum)    spectrum.render();
        if (panel.show_spectrogram) spectrogram.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    flowgraph.stop();
    panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << usrp.get_overflow_count() << std::endl;
    return 0;
}
