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
};
