#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "spectral_windows.hpp"
#include "imgui.h"
#include <vector>
#include <mutex> 
#include <vector>

struct PlotCSpectrumBlock : public cler::BlockBase {
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(const char* name,
                       const std::vector<std::string>& signal_labels,
                       size_t sps,
                       size_t n_fft_samples,
                       SpectralWindow window_type = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrumBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

    // One-shot programmatic window rect: the next render() applies it with
    // ImGuiCond_Always and clears the request, so the user can still move or
    // resize the window afterward. Both the setter and render() run on the GUI
    // thread (procedure() never touches these), so plain members are fine.
    void apply_window_rect(float x, float y, float w, float h);

    // Enable/disable this block's work from outside (e.g. when its window is
    // hidden). While INACTIVE, procedure() still fully drains its input
    // channels each call -- so an upstream fanout never stalls on this branch
    // -- but the samples are DROPPED, never copied into the internal plot
    // ring. This is distinct from the Pause button (_gui_pause), which only
    // freezes the displayed snapshot while data keeps flowing underneath.
    // Defaults to active, so existing users of this block are unaffected.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    // GUI-THREAD-ONLY export of the currently displayed (averaged) spectrum
    // for one channel: freq_hz gets the frequency axis (baseband Hz, size
    // n_fft), mag_db the averaged magnitudes. No lock is needed: _freq_bins
    // and _spectrum_avg are written only in the constructor and in render(),
    // which runs on the same (GUI) thread as this accessor. Returns false if
    // render() has not produced a spectrum yet or `channel` is out of range.
    bool export_spectrum(size_t channel, std::vector<float>& freq_hz,
                         std::vector<float>& mag_db) const;

private:
    void next_window_geometry();   // SetNextWindowPos/Size before Begin()

    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;
    size_t _buffer_size;
    SpectralWindow _window_type;

    cler::Channel<std::complex<float>>* _signal_channels;

    std::complex<float>** _snapshot_buffers = nullptr;
    std::complex<float>* _tmp_buffer = nullptr;

    std::complex<float>* _liquid_inout = nullptr;
    float* _freq_bins = nullptr;
    float* _tmp_mag_buffer = nullptr;
    float** _spectrum_avg = nullptr;  // Averaged spectrum for each input
    float _avg_alpha = 0.7f;          // Exponential averaging factor (0=frozen, 1=no averaging)
    bool _first_spectrum = true;      // First spectrum frame flag

    fftplan _fftplan;

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos {0.0f, 0.0f};
    ImVec2 _pending_rect_size {0.0f, 0.0f};

    std::mutex _snapshot_mutex;
    size_t _snapshot_ready_size = 0;

    std::atomic<bool> _gui_pause = false;

    // set_active(false): drop (drain) input instead of plotting it. See the
    // comment on set_active() for how this differs from _gui_pause.
    std::atomic<bool> _external_pause{false};
};
