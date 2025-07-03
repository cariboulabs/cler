#pragma once

#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include <complex>
#include <cmath>

#include "liquid.h"

struct PlotSpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotSpectrumBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t sps, size_t buffer_size, size_t work_size) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps), _buffer_size(buffer_size), _work_size(work_size)
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotSpectrumBlock requires at least one input channel");
        }
        if (sps == 0) {
            throw std::invalid_argument("Samples per second (sps) must be greater than zero.");
        }
        if (buffer_size < work_size) {
            throw std::invalid_argument("Buffer size must be greater than or equal to work size.");
        }
        if ((buffer_size & (buffer_size - 1)) != 0) {
            throw std::invalid_argument("Buffer size must be a power of two.");
        }

        // Allocate channels for complex input
        in = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<std::complex<float>>(buffer_size);
        }

        // Allocate time buffers as cler::Channel
        _time_buffers = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&_time_buffers[i]) cler::Channel<std::complex<float>>(buffer_size);
        }

        //allocate temporary buffer Moving data between inputs and time buffers
        _tmp = new std::complex<float>[buffer_size];

        // Allocate magnitude spectrum buffers
        _spectrum_buffers = new float*[num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _spectrum_buffers[i] = new float[buffer_size];
        }

        // Allocate frequency bins
        _freq_bins = new float[buffer_size];
        for (size_t i = 0; i < buffer_size; ++i) {
            _freq_bins[i] = i * (static_cast<float>(_sps) / static_cast<float>(buffer_size));
        }

        // Liquid DSP: allocate input/output buffers
        _liquid_inout = new std::complex<float>[buffer_size];

        // Create FFT plan
        _fftplan = fft_create_plan(buffer_size, _liquid_inout, _liquid_inout, LIQUID_FFT_FORWARD, 0);
    }

    ~PlotSpectrumBlock() {
        delete[] in;
        
        
        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _spectrum_buffers[i];
        }
        delete[] _time_buffers;
        delete[] _tmp;
        delete[] _spectrum_buffers;

        delete[] _freq_bins;
        delete[] _liquid_inout;

        fft_destroy_plan(_fftplan);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (in[i].size() < _work_size) {
                return cler::Error::NotEnoughSamples;
            }
        }

        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].peek_read(_tmp, _work_size);
            in[i].readN(_time_buffers[i], _work_size);

            // Copy input to Liquid input buffer
            for (size_t j = 0; j < _work_size; ++j) {
                _liquid_inout[j] = _time_buffers[i][j];
            }

            // Execute FFT
            fft_execute(_fftplan);

            // Compute magnitude spectrum
            for (size_t k = 0; k < _work_size; ++k) {
                float real = _liquid_inout[k].real();
                float imag = _liquid_inout[k].imag();
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
                ImPlot::PlotLine(_signal_labels[i], _freq_bins, _spectrum_buffers[i], _buffer_size);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    size_t _num_inputs;
    const char** _signal_labels;
    size_t _sps;

    cler::Channel<std::complex<float>>* _time_buffers;  // [num_inputs][buffer_size]
    std::complex<float>* _tmp; //[buffer_size]
    float** _spectrum_buffers;            // [num_inputs][buffer_size]
    float* _freq_bins;                    // [buffer_size]

    std::complex<float>* _liquid_inout;   // single in/out buffer
    fftplan _fftplan;                     // Liquid-DSP FFT plan

    size_t _buffer_size;
    size_t _work_size;
};
