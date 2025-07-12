#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "imgui.h"
#include "spectral_windows.hpp"

struct PlotCSpectrumBlock : public cler::BlockBase {
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    cler::Channel<std::complex<float>>* in;

    // Construct & destruct
    PlotCSpectrumBlock(std::string name,
                       const std::vector<std::string> signal_labels,
                       const size_t sps,
                       const size_t n_fft_samples,
                       const SpectralWindow window_type  = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrumBlock();

    // Called in the flowgraph
    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

private:
    size_t _num_inputs;
    std::vector<std::string> _signal_labels;

    size_t _sps;
    size_t _n_fft_samples;
    SpectralWindow _window_type;

    // FFT plan and buffers
    std::complex<float>* _liquid_inout;
    float* _tmp_mag_buffer;

    std::complex<float>** _buffers0; // [num_inputs][n_fft_samples]
    std::complex<float>** _buffers1; // [num_inputs][n_fft_samples]
    std::atomic<uint8_t> _show_buffer = 0;
    float* _freq_bins;

    fftplan _fftplan;

    // GUI settings
    ImVec2 _initial_window_position = ImVec2(200, 200);
    ImVec2 _initial_window_size = ImVec2(600, 400);
    bool _has_initial_window_position = false;
};
