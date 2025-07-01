#pragma once

#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include <complex>
#include <vector>
#include <cmath>

// You may want to link/link a simple FFT implementation, e.g., KissFFT or use your own
#include <fftw3.h>

struct PlotSpectrumBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    PlotSpectrumBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t work_size, float sample_rate) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _work_size(work_size), _sample_rate(sample_rate)
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotSpectrumBlock requires at least one input channel");
        }
        if (work_size == 0) {
            throw std::invalid_argument("Work size must be greater than zero.");
        }

        in = static_cast<cler::Channel<float>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<float>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<float>(2 * work_size);
        }

        _time_buffers.resize(num_inputs);
        _spectrum_buffers.resize(num_inputs);
        _freq_bins.resize(work_size / 2); // Positive freqs only for real input

        // Prepare frequency axis
        for (size_t i = 0; i < _freq_bins.size(); ++i) {
            _freq_bins[i] = i * (_sample_rate / _work_size);
        }

        for (size_t i = 0; i < num_inputs; ++i) {
            _time_buffers[i].resize(work_size);
            _spectrum_buffers[i].resize(work_size / 2);
        }

        _fft_cfg = kiss_fft_alloc(_work_size, 0, nullptr, nullptr);
    }

    ~PlotSpectrumBlock() {
        delete[] in;
        kiss_fft_free(_fft_cfg);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (in[i].size() < _work_size) {
                return cler::Error::NotEnoughSamples;
            }
        }

        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].readN(_time_buffers[i].data(), _work_size);

            // Convert to complex input for FFT
            std::vector<kiss_fft_cpx> in_cpx(_work_size);
            std::vector<kiss_fft_cpx> out_cpx(_work_size);

            for (size_t j = 0; j < _work_size; ++j) {
                in_cpx[j].r = _time_buffers[i][j];
                in_cpx[j].i = 0.0f;
            }

            kiss_fft(_fft_cfg, in_cpx.data(), out_cpx.data());

            // Compute magnitude spectrum (positive frequencies only)
            for (size_t k = 0; k < _work_size / 2; ++k) {
                float mag = std::sqrt(out_cpx[k].r * out_cpx[k].r + out_cpx[k].i * out_cpx[k].i);
                _spectrum_buffers[i][k] = 20.0f * std::log10(mag + 1e-8f); // dB scale
            }
        }

        return cler::Empty{};
    }

    void render() {
        ImGui::Begin("PlotSpectrum");
        if (ImPlot::BeginPlot(name())) {
            ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");
            for (size_t i = 0; i < _num_inputs; ++i) {
                ImPlot::PlotLine(_signal_labels[i], _freq_bins.data(), _spectrum_buffers[i].data(), _freq_bins.size());
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    size_t _num_inputs;
    const char** _signal_labels;
    size_t _work_size;
    float _sample_rate;

    std::vector<std::vector<float>> _time_buffers;
    std::vector<std::vector<float>> _spectrum_buffers;
    std::vector<float> _freq_bins;

    kiss_fft_cfg _fft_cfg;
};
