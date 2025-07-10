#pragma once
#include "cler.hpp"
#include <vector>
#include "liquid.h"
#include "imgui.h"

struct PlotCSpectrumBlock : public cler::BlockBase {
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(std::string name, const std::vector<std::string> signal_labels,
        const size_t sps, const size_t buffer_size);
    ~PlotCSpectrumBlock();
    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

private:
    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;

    cler::Channel<std::complex<float>>* _y_channels;  // ring buffers for each signal
    float* _freq_bins;

    std::atomic<size_t> _snapshot_ready_size = 0;
    std::atomic<bool> _snapshot_requested = false;
    std::complex<float>** _snapshot_y_buffers = nullptr;

    std::complex<float>* _tmp_y_buffer = nullptr;
    float* _tmp_magnitude_buffer = nullptr;

    std::complex<float>* _liquid_inout;
    fftplan _fftplan;

    std::atomic<bool> _gui_pause = false;
    bool _has_initial_window_position = false;
    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};
