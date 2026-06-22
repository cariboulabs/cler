// Slim "Spike-like" spectrum analyzer GUI for USRP on CLER.
//
// Milestone 1: reliable zero-span (power-vs-time) capture with a real trigger,
// plus a spectrum view for context, all driven from one live control panel.
//
//   USRP --> Fanout(2) --+--> PowerDetector --> Trigger --> PlotTimeSeries (zero-span)
//                        +--> PlotCSpectrum                  (spectrum context)
//
// Sample rate (span) is fixed at startup; center frequency and gain are live.

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/triggers/trigger_block.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "power_detector.hpp"

#include <chrono>
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
                 const SpikeArgs& a)
        : _src(src), _trig(trig),
          _freq_mhz(static_cast<float>(a.freq / 1e6)),
          _gain_db(static_cast<float>(a.gain)),
          _rate_hz(a.rate),
          _max_window_ms(trig->max_window_ms()) {}

    void render() {
        ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Control");

        ImGui::TextUnformatted("Radio");
        ImGui::Separator();
        ImGui::Text("Sample rate (span): %.3f MS/s  [fixed]", _rate_hz / 1e6);

        // Apply freq/gain only when the user finishes editing, not every tick,
        // so we don't spam the USRP with retunes.
        ImGui::SetNextItemWidth(180);
        ImGui::DragFloat("Center (MHz)", &_freq_mhz, 0.1f, 0.0f, 6000.0f, "%.3f");
        bool freq_done = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Gain (dB)", &_gain_db, 0.0f, 76.0f, "%.1f");
        bool gain_done = ImGui::IsItemDeactivatedAfterEdit();
        if (freq_done || gain_done) push_radio_config();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Trigger");
        ImGui::Separator();

        bool changed = false;
        changed |= ImGui::SliderFloat("Level (dB)", &_threshold_db, -120.0f, 0.0f, "%.1f");
        help("Power level the signal must cross to fire a trigger.");
        changed |= ImGui::SliderFloat("Hysteresis (dB)", &_hysteresis_db, 0.0f, 30.0f, "%.1f");
        help("Dead-band. After a rising trigger fires, the signal must drop below "
             "(Level - Hysteresis) before it can fire again. Stops chatter when the "
             "signal hovers right at the Level.");
        changed |= ImGui::SliderFloat("Window (ms)", &_window_ms, 1.0f, _max_window_ms, "%.1f");
        help("Total time span captured and displayed per trigger (pre + post). "
             "This is the scope timebase.");
        changed |= ImGui::SliderFloat("Pre-trigger (%)", &_pretrigger_pct, 0.0f, 90.0f, "%.0f");
        help("How much of the Window is shown BEFORE the trigger instant (t=0). "
             "e.g. 10% puts the trigger 10% in from the left.");
        changed |= ImGui::SliderFloat("Holdoff (ms)", &_holdoff_ms, 0.0f, 1000.0f, "%.0f");
        help("Minimum time after a trigger before another can fire. Suppresses "
             "re-triggering on ringing or the same burst.");
        changed |= ImGui::SliderFloat("Auto timeout (ms)", &_auto_ms, 10.0f, 2000.0f, "%.0f");
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

    float  _freq_mhz;
    float  _gain_db;
    double _rate_hz;

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
};

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

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
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

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel(&usrp, &trigger, args);

    // Trigger is a sink: it consumes the power stream and renders the captured
    // window itself (oscilloscope style), so it has no downstream channel.
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp,    &fanout.in),
        cler::BlockRunner(&fanout,  &power.in, &spectrum.in[0]),
        cler::BlockRunner(&power,   &trigger.in),
        cler::BlockRunner(&trigger),
        cler::BlockRunner(&spectrum)
    );

    flowgraph.run();
    std::cout << "CLER Spike running at " << args.freq / 1e6 << " MHz, "
              << args.rate / 1e6 << " MS/s. Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        panel.render();
        trigger.render();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    flowgraph.stop();
    std::cout << "Overflows: " << usrp.get_overflow_count() << std::endl;
    return 0;
}
