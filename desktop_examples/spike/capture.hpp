#pragma once

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "desktop_blocks/gui/gui_manager.hpp"
#include "control_panel.hpp"
#include "snapshot.hpp"
#include "spike_args.hpp"

// --capture state. Snapshots wait for the trigger's published-frame counter
// to advance, so the image always shows a real capture rather than whatever
// the scope happened to hold at startup. The warmup lets the first tiling
// and the plots draw before anything is grabbed.
struct CaptureBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    CaptureBlock(const char* name, const SpikeArgs& a,
                 ControlPanel* panel, Trig* trig,
                 PlotCSpectrumBlock* spectrum,
                 PlotCSpectrogramBlock* sgram,
                 cler::GuiManager* gui)
        : BlockBase(name),
          _capture_dir(a.capture_dir),
          _capture_frames(a.capture_frames),
          _capture_timeout_s(a.capture_timeout_s),
          _capture_exit(a.capture_exit),
          _capture_force(a.capture_force),
          _capture_no_dat(a.capture_no_dat),
          _panel(panel), _trig(trig), _spectrum(spectrum), _sgram(sgram),
          _gui(gui) {
        if (capture_mode()) {
            std::cout << "capture: dir " << _capture_dir << ", waiting for "
                      << _capture_frames << " trigger frame(s)"
                      << (_capture_force ? " (forced)" : "") << std::endl;
            if (!_capture_exit)
                std::cout << "capture: --capture-exit not given, window stays open "
                             "after the last snapshot" << std::endl;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    bool capture_mode() const { return !_capture_dir.empty(); }
    bool timed_out() const { return _capture_timed_out; }

    void render() {
        // Drive unattended snapshots the same way the button does (set the
        // one-shot flag), so both paths share the write code below.
        if (capture_mode() && _captures_done < _capture_frames) {
            if (_capture_warmup < kCaptureWarmupFrames) {
                ++_capture_warmup;
            } else {
                if (!_capture_waiting) {
                    _capture_baseline   = _trig->frame_count();
                    _capture_wait_start = std::chrono::steady_clock::now();
                    _capture_waiting    = true;
                    if (_capture_force) _trig->force_trigger();
                }
                const double waited = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - _capture_wait_start).count();
                const bool have_frame = _trig->frame_count() != _capture_baseline;
                const bool timed_out  = _capture_timeout_s > 0.0 &&
                                        waited >= _capture_timeout_s;
                if (have_frame || timed_out) {
                    if (timed_out && !have_frame) {
                        std::cerr << "capture: no trigger frame within "
                                  << _capture_timeout_s
                                  << " s; snapshotting the current view anyway"
                                  << std::endl;
                        _capture_timed_out = true;
                    }
                    _panel->snapshot_requested = true;
                    _capture_waiting = false;
                    ++_captures_done;
                    // A timeout means the trigger isn't firing, so don't sit
                    // through the same wait again for the remaining frames.
                    if (_capture_timed_out) _captures_done = _capture_frames;
                    if (_captures_done >= _capture_frames) _capture_finished = true;
                }
            }
        }

        // Consumed after render() (exported data matches this frame) and before end_frame()'s screenshot pass.
        if (_panel->snapshot_requested) {
            _panel->snapshot_requested = false;
            std::string base = snapshot_base_path(_panel->snapshot_dir());
            if (base.empty()) {
                _panel->set_status("Snapshot failed: no free filename in " +
                                   _panel->snapshot_dir());
                if (capture_mode())
                    std::cerr << "capture: no free filename in "
                              << _panel->snapshot_dir() << std::endl;
            } else {
                std::string err;
                const bool want_dat = !_capture_no_dat;
                bool dat_ok = true;
                if (want_dat) {
                    dat_ok = write_snapshot_dat(
                        base + ".dat",
                        _panel->show_scope, _panel->show_spectrum, _panel->show_spectrogram,
                        _panel->rate_hz(),
                        static_cast<double>(_panel->detection_rate()),
                        static_cast<double>(_panel->freq_mhz()),
                        *_trig, *_spectrum, *_sgram, err);
                }
                _gui->request_screenshot(base + ".png");   // written in end_frame()
                // Absolute path so files are easy to locate even if snapshot dir was relative.
                std::string full =
                    std::filesystem::absolute(base).lexically_normal().string();
                const std::string tail = !want_dat ? std::string()
                                       : dat_ok    ? std::string("/.dat")
                                                   : "; .dat failed: " + err;
                _panel->set_status("Saved " + full + ".png" + tail);
                // stdout so an unattended caller can read the path it just got.
                if (capture_mode()) {
                    std::cout << "capture: wrote " << full << ".png"
                              << (!want_dat ? "" : dat_ok ? " (+ .dat)"
                                                          : " (.dat failed)")
                              << std::endl;
                }
            }
        }

        if (_capture_finished && _capture_exit) _gui->request_close();
    }

private:
    std::string _capture_dir;
    int    _capture_frames;
    double _capture_timeout_s;
    bool   _capture_exit;
    bool   _capture_force;
    bool   _capture_no_dat;

    ControlPanel* _panel;
    Trig* _trig;
    PlotCSpectrumBlock* _spectrum;
    PlotCSpectrogramBlock* _sgram;
    cler::GuiManager* _gui;

    static constexpr int kCaptureWarmupFrames = 45;   // ~0.75 s at 60 fps
    int  _capture_warmup     = 0;
    int  _captures_done      = 0;
    bool _capture_waiting    = false;          // baseline taken, watching for a new frame
    bool _capture_timed_out  = false;
    bool _capture_finished   = false;          // last snapshot written this frame
    unsigned long _capture_baseline = 0;
    std::chrono::steady_clock::time_point _capture_wait_start{};
};
