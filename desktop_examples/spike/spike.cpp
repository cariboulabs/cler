// Slim "Spike-like" spectrum analyzer GUI for USRP / HackRF / Pluto on CLER.
// All blocks always run; a staged rate change re-syncs every consumer once the
// ACTUAL hardware rate lands.

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include "spike_source.hpp"
#include "spike_args.hpp"
#include "control_panel.hpp"
#include "snapshot.hpp"
#include "capture.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using Trig = TriggerBlock<float>;
using PowerDet = PowerDetectorBlock<std::complex<float>>;

static const char* window_title_for(SourceKind kind) {
    switch (kind) {
        case SourceKind::HackRF: return "CLER Spike - HackRF";
        case SourceKind::Pluto:  return "CLER Spike - Pluto";
        case SourceKind::UHD:    break;
    }
    return "CLER Spike - USRP";
}

static int run_app(SpikeArgs& args) {
    cler::GuiManager gui(1500, 900, window_title_for(args.source));
    // Vsync already paces this loop; the tiny sleep just keeps CPU low if vsync is off/bypassed.
    gui.set_frame_sleep_ms(2);

    ImGuiIO& io = ImGui::GetIO();
    static std::string imgui_ini = config_path(".cler_spike_imgui.ini");
    io.IniFilename = imgui_ini.c_str();

    SpikeSourceBlock source("Source", args.source, args.freq, args.rate,
                            args.device_address, args.gain,
                            args.lna, args.vga, args.amp);
    args.rate = source.actual_sample_rate();
    ISource& src_if = source;

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
    //
    // The ring holds `waterfall_rows` rows; each row spans at minimum one FFT
    // frame = n_fft/rate seconds (frames/row = 1). So the SHORTEST visible
    // history is waterfall_rows * n_fft / rate. The default 2000 rows gives
    // ~4.1 s at 1 MS/s / 2048-pt; --history sizes the ring to reach a shorter
    // floor (rows = history * rate / n_fft), clamped to a usable range. The
    // in-window History slider still scales UP from this floor (frames/row).
    size_t waterfall_rows = 2000;
    if (args.history_s > 0.0) {
        double rows = std::round(args.history_s * args.rate
                                 / static_cast<double>(args.fft));
        rows = std::min(std::max(rows, 16.0), 20000.0);
        waterfall_rows = static_cast<size_t>(rows);
    }
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"I/Q"},
        static_cast<size_t>(args.rate), args.fft, waterfall_rows);

    ChannelizerPanelBlock channelizer("Channelizer");

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    spectrogram.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    channelizer.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel("ControlPanel", &src_if, &trigger, &spectrum, &spectrogram,
                       &power, &channelizer, waterfall_rows, args);
    const std::string settings_file = config_path(".cler_spike.conf");
    panel.load(settings_file);   // restore last session's settings if present

    // Capture mode overrides two loaded settings: the destination directory, and
    // the scope's visibility (a conf that hid it would yield useless snapshots).
    if (args.capture_mode()) {
        std::error_code ec;
        std::filesystem::create_directories(args.capture_dir, ec);
        if (!std::filesystem::is_directory(args.capture_dir)) {
            std::cerr << "Error: --capture dir " << args.capture_dir
                      << " is not usable: " << ec.message() << "\n";
            return 1;
        }
        panel.set_snapshot_dir(args.capture_dir);
        panel.show_scope = true;
    }

    CaptureBlock capture("Capture", args, &panel, &trigger, &spectrum,
                         &spectrogram, &gui);

    // Trigger is a sink: renders its own capture (oscilloscope-style), no downstream channel.
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&panel),
        cler::BlockRunner(&source,  &fanout.in),
        cler::BlockRunner(&fanout,  &power.in, &spectrum.in[0], &spectrogram.in[0],
                                    &channelizer.in),
        cler::BlockRunner(&power,   &trigger.in),
        cler::BlockRunner(&trigger),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&channelizer),
        cler::BlockRunner(&capture)
    );

    flowgraph.run();
    panel.apply_all();   // sync radio + trigger to loaded/initial settings
    // Report what the panel actually applied; args.* may be stale by now.
    std::cout << "CLER Spike running at " << panel.freq_mhz() << " MHz, "
              << panel.rate_hz() / 1e6 << " MS/s. Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    // Capture runs must not persist settings: --capture overrides the snapshot
    // dir and scope visibility, and an unattended run would overwrite the
    // user's tuned conf with them.
    if (!args.capture_mode())
        panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << src_if.get_overflow_count() << std::endl;
    return capture.timed_out() ? 2 : 0;
}

int main(int argc, char** argv) {
    SpikeArgs args = parse_args(argc, argv);
    // Ask the device what it accepts before the source constructor, which panics.
    std::string why = check_and_clamp_source(args.source, args.device_address,
                                             args.freq, args.rate);
    if (!why.empty()) {
        std::cerr << "Error: " << why << "\n";
        return 1;
    }
    return run_app(args);
}
