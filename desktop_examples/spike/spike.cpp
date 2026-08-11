// Slim "Spike-like" spectrum analyzer GUI for USRP / HackRF / Pluto on CLER.
// All blocks always run; a staged rate change re-syncs every consumer once the
// ACTUAL hardware rate lands.

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include "spike_source.hpp"
#include "spike_args.hpp"
#include "control_panel.hpp"
#include "snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

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

    ControlPanel panel(&src_if, &trigger, &spectrum, &spectrogram, &power,
                       &channelizer, waterfall_rows, args);
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

    // --capture state. Snapshots wait for the trigger's published-frame counter
    // to advance, so the image always shows a real capture rather than whatever
    // the scope happened to hold at startup. The warmup lets the first tiling
    // and the plots draw before anything is grabbed.
    constexpr int kCaptureWarmupFrames = 45;   // ~0.75 s at 60 fps
    int  capture_warmup     = 0;
    int  captures_done      = 0;
    bool capture_waiting    = false;          // baseline taken, watching for a new frame
    bool capture_timed_out  = false;
    bool capture_finished   = false;          // last snapshot written this frame
    unsigned long capture_baseline = 0;
    auto capture_wait_start = std::chrono::steady_clock::now();
    if (args.capture_mode()) {
        std::cout << "capture: dir " << args.capture_dir << ", waiting for "
                  << args.capture_frames << " trigger frame(s)"
                  << (args.capture_force ? " (forced)" : "") << std::endl;
        if (!args.capture_exit)
            std::cout << "capture: --capture-exit not given, window stays open "
                         "after the last snapshot" << std::endl;
    }

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

        // Drive unattended snapshots the same way the button does (set the
        // one-shot flag), so both paths share the write code below.
        if (args.capture_mode() && captures_done < args.capture_frames) {
            if (capture_warmup < kCaptureWarmupFrames) {
                ++capture_warmup;
            } else {
                if (!capture_waiting) {
                    capture_baseline   = trigger.frame_count();
                    capture_wait_start = std::chrono::steady_clock::now();
                    capture_waiting    = true;
                    if (args.capture_force) trigger.force_trigger();
                }
                const double waited = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - capture_wait_start).count();
                const bool have_frame = trigger.frame_count() != capture_baseline;
                const bool timed_out  = args.capture_timeout_s > 0.0 &&
                                        waited >= args.capture_timeout_s;
                if (have_frame || timed_out) {
                    if (timed_out && !have_frame) {
                        std::cerr << "capture: no trigger frame within "
                                  << args.capture_timeout_s
                                  << " s; snapshotting the current view anyway"
                                  << std::endl;
                        capture_timed_out = true;
                    }
                    panel.snapshot_requested = true;
                    capture_waiting = false;
                    ++captures_done;
                    // A timeout means the trigger isn't firing, so don't sit
                    // through the same wait again for the remaining frames.
                    if (capture_timed_out) captures_done = args.capture_frames;
                    if (captures_done >= args.capture_frames) capture_finished = true;
                }
            }
        }

        // Consumed after render() (exported data matches this frame) and before end_frame()'s screenshot pass.
        if (panel.snapshot_requested) {
            panel.snapshot_requested = false;
            std::string base = snapshot_base_path(panel.snapshot_dir());
            if (base.empty()) {
                panel.set_status("Snapshot failed: no free filename in " +
                                 panel.snapshot_dir());
                if (args.capture_mode())
                    std::cerr << "capture: no free filename in "
                              << panel.snapshot_dir() << std::endl;
            } else {
                std::string err;
                const bool want_dat = !args.capture_no_dat;
                bool dat_ok = true;
                if (want_dat) {
                    dat_ok = write_snapshot_dat(
                        base + ".dat",
                        panel.show_scope, panel.show_spectrum, panel.show_spectrogram,
                        panel.rate_hz(),
                        static_cast<double>(panel.detection_rate()),
                        static_cast<double>(panel.freq_mhz()),
                        trigger, spectrum, spectrogram, err);
                }
                gui.request_screenshot(base + ".png");   // written in end_frame()
                // Absolute path so files are easy to locate even if snapshot dir was relative.
                std::string full =
                    std::filesystem::absolute(base).lexically_normal().string();
                const std::string tail = !want_dat ? std::string()
                                       : dat_ok    ? std::string("/.dat")
                                                   : "; .dat failed: " + err;
                panel.set_status("Saved " + full + ".png" + tail);
                // stdout so an unattended caller can read the path it just got.
                if (args.capture_mode()) {
                    std::cout << "capture: wrote " << full << ".png"
                              << (!want_dat ? "" : dat_ok ? " (+ .dat)"
                                                          : " (.dat failed)")
                              << std::endl;
                }
            }
        }

        gui.end_frame();   // the screenshot requested above is written in here
        if (capture_finished && args.capture_exit) break;
        // Vsync already paces this loop; the tiny sleep just keeps CPU low if vsync is off/bypassed.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    flowgraph.stop();
    // Capture runs must not persist settings: --capture overrides the snapshot
    // dir and scope visibility, and an unattended run would overwrite the
    // user's tuned conf with them.
    if (!args.capture_mode())
        panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << src_if.get_overflow_count() << std::endl;
    return capture_timed_out ? 2 : 0;
}

int main(int argc, char** argv) {
    SpikeArgs args = parse_args(argc, argv);
    return run_app(args);
}
