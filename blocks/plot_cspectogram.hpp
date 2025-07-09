#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "liquid.h"
#include <complex>

struct PlotCSpectrogramBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotCSpectrogramBlock(const char* name, const size_t num_inputs, const char** signal_labels,
        const size_t sps, const size_t buffer_size, const size_t tall) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps), _buffer_size(buffer_size), _tall(tall)
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotCSpectrogramBlock requires at least one input channel");
        }
        if (buffer_size <= 2 || tall < 1) {
            throw std::invalid_argument("Buffer size and tall must be > 0");
        }

        // Allocate input channels
        in = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<std::complex<float>>(2*_buffer_size);
        }

        // Allocate FFT plan and temporary buffers
        _liquid_inout = new std::complex<float>[_buffer_size];
        _tmp_y_buffer = new std::complex<float>[_buffer_size];
        _tmp_magnitude_buffer = new float[_buffer_size];
        _fftplan = fft_create_plan(_buffer_size, 
            reinterpret_cast<liquid_float_complex*>(_liquid_inout),
            reinterpret_cast<liquid_float_complex*>(_liquid_inout),
            LIQUID_FFT_FORWARD, 0);

        // Allocate spectrogram buffer for each input: tall x buffer_size
        _spectrograms = new float*[_num_inputs];
        for (size_t i = 0; i < _num_inputs; ++i) {
            _spectrograms[i] = new float[_tall * _buffer_size]();
        }

        _freq_bins = new float[_buffer_size];
        for (size_t i = 0; i < _buffer_size; ++i) {
            _freq_bins[i] = i * (static_cast<float>(_sps) / static_cast<float>(_buffer_size));
        }
    }

    ~PlotCSpectrogramBlock() {
        using ComplexChannel = cler::Channel<std::complex<float>>;
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].~ComplexChannel();
        }
        ::operator delete[](in);

        delete[] _liquid_inout;
        delete[] _tmp_y_buffer;
        delete[] _tmp_magnitude_buffer;

        fft_destroy_plan(_fftplan);

        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _spectrograms[i];
        }
        delete[] _spectrograms;

        delete[] _freq_bins;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = in[0].size();
        for (size_t i = 1; i < _num_inputs; ++i) {
            if (in[i].size() < available) {
                available = in[i].size();
            }
        }
        if (available < _buffer_size) {
            return cler::Error::NotEnoughSamples;
        }

        for (size_t i = 0; i < _num_inputs; ++i) {
           in[i].readN(_tmp_y_buffer, _buffer_size);

            // Assume we want exactly buffer_size for the FFT
            memcpy(_liquid_inout, _tmp_y_buffer, _buffer_size * sizeof(std::complex<float>));
            fft_execute(_fftplan);

            // Compute magnitude
            for (size_t j = 0; j < _buffer_size; ++j) {
                float re = _liquid_inout[j].real();
                float im = _liquid_inout[j].imag();
                _tmp_magnitude_buffer[j] = 10.0f * log10f(sqrtf(re * re + im * im) + 1e-15f);
            }

            // Push magnitude row into spectrogram: shift up
            memmove(
                _spectrograms[i] + _buffer_size,
                _spectrograms[i],
                (_tall - 1) * _buffer_size * sizeof(float)
            );
            memcpy(
                _spectrograms[i],
                _tmp_magnitude_buffer,
                _buffer_size * sizeof(float)
            );
        }
        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());

        for (size_t i = 0; i < _num_inputs; ++i) {
            if (ImPlot::BeginPlot(_signal_labels[i])) {
                ImPlot::SetupAxes("Frequency (Hz)", "Time (frames)");

                ImPlot::PlotHeatmap(
                    _signal_labels[i],
                    _spectrograms[i],
                    _tall,
                    _buffer_size,
                    -160.0, 30.0,
                    nullptr,
                    ImPlotPoint(0, 0),
                    ImPlotPoint(static_cast<double>(_sps), static_cast<double>(_tall))
                );

                ImPlot::EndPlot();
            }
        }
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
        _has_initial_window_position = true;
    }

private:
    size_t _num_inputs;
    const char** _signal_labels;
    size_t _sps;
    size_t _buffer_size;
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
