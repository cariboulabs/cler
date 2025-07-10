#pragma once
#include "cler.hpp"
#include "liquid.h"
#include "imgui.h"

struct PlotCSpectrogramBlock : public cler::BlockBase {
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrogramBlock(std::string name, std::vector<std::string> signal_labels,
        const size_t sps, const size_t n_fft_samples, const size_t tall);
    ~PlotCSpectrogramBlock();
    cler::Result<cler::Empty, cler::Error> procedure();

    void render();

    void set_initial_window(float x, float y, float w, float h);

private:
    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;
    size_t _tall;

    std::complex<float>* _liquid_inout;
    std::complex<float>* _tmp_y_buffer;
    float* _tmp_magnitude_buffer;
    float** _spectrograms;

    fftplan _fftplan;
    float* _freq_bins;

    bool _has_initial_window_position = false;
    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};
