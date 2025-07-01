#pragma once

#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include <complex>
#include <cmath>

#include <fftw3.h>

struct PlotSpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotSpectrumBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t work_size, float sample_rate) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _work_size(work_size), _sample_rate(sample_rate)
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotSpectrumBlock requires at least one input channel");
        }
        if (work_size == 0) {
            throw std::invalid_argument("Work size must be greater than zero.");
        }

        // Allocate channels for complex input
        in = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<std::complex<float>>(2 * work_size);
        }

        // Allocate time buffers
        _time_buffers = new std::complex<float>*[num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _time_buffers[i] = new std::complex<float>[work_size];
        }

        // Allocate magnitude spectrum buffers
        _spectrum_buffers = new float*[num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _spectrum_buffers[i] = new float[work_size];
        }

        // Allocate frequency bins
        _freq_bins = new float[work_size];
        for (size_t i = 0; i < work_size; ++i) {
            _freq_bins[i] = i * (_sample_rate / _work_size);
        }

        // FFTW: allocate complex buffers and plan
        _fftw_in_cpx = fftwf_alloc_complex(_work_size);
        _fftw_out_cpx = fftwf_alloc_complex(_work_size);
        _fftw_plan = fftwf_plan_dft_1d(
            _work_size, _fftw_in_cpx, _fftw_out_cpx, FFTW_FORWARD, FFTW_MEASURE
        );
    }

    ~PlotSpectrumBlock() {
        delete[] in;

        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _time_buffers[i];
            delete[] _spectrum_buffers[i];
        }
        delete[] _time_buffers;
        delete[] _spectrum_buffers;

        delete[] _freq_bins;

        fftwf_destroy_plan(_fftw_plan);
        fftwf_free(_fftw_in_cpx);
        fftwf_free(_fftw_out_cpx);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (in[i].size() < _work_size) {
                return cler::Error::NotEnoughSamples;
            }
        }

        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].readN(_time_buffers[i], _work_size);

            // Copy input to FFTW input buffer
            for (size_t j = 0; j < _work_size; ++j) {
                _fftw_in_cpx[j][0] = _time_buffers[i][j].real();
                _fftw_in_cpx[j][1] = _time_buffers[i][j].imag();
            }

            fftwf_execute(_fftw_plan);

            // Compute magnitude spectrum
            for (size_t k = 0; k < _work_size; ++k) {
                float real = _fftw_out_cpx[k][0];
                float imag = _fftw_out_cpx[k][1];
                float mag = std::sqrt(real * real + imag * imag);
                _spectrum_buffers[i][k] = 20.0f * std::log10(mag + 1e-15f);
            }
        }

        return cler::Empty{};
    }

    void render() {
        ImGui::Begin("PlotSpectrum");
        if (ImPlot::BeginPlot(name())) {
            ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");
            for (size_t i = 0; i < _num_inputs; ++i) {
                ImPlot::PlotLine(_signal_labels[i], _freq_bins, _spectrum_buffers[i], _work_size);
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

    std::complex<float>** _time_buffers; // [num_inputs][work_size]
    float** _spectrum_buffers;            // [num_inputs][work_size]
    float* _freq_bins;                    // [work_size]

    fftwf_plan _fftw_plan;
    fftwf_complex* _fftw_in_cpx;
    fftwf_complex* _fftw_out_cpx;
};
