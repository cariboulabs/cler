#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "liquid.h"

struct PlotCSpectrogramBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotCSpectrogramBlock(std::string name, const size_t num_inputs, std::vector<std::string> signal_labels,
        const size_t sps, const size_t buffer_size, const size_t tall) 
        : BlockBase(std::move(name)), _num_inputs(num_inputs), _signal_labels(std::move(signal_labels)), _sps(sps), _buffer_size(buffer_size), _tall(tall)
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotCSpectrogramBlock requires at least one input channel");
        }
        if (buffer_size <= 2 || tall < 1) {
            throw std::invalid_argument("Buffer size and tall must be > 0");
        }
        if (buffer_size % 2 != 0) {
            throw std::invalid_argument("Buffer size must be even.");
        }

        // Allocate input channels
        in = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<std::complex<float>>(5*_buffer_size);
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
            std::fill_n(_spectrograms[i], _tall * _buffer_size, -147.0f);
        }

        _freq_bins = new float[_buffer_size];
        for (size_t i = 0; i < _buffer_size; ++i) {
            _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(buffer_size))) - (_sps / 2.0f);
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

            memcpy(_liquid_inout, _tmp_y_buffer, _buffer_size * sizeof(std::complex<float>));
            
            float coherent_gain = 0.0f;
            for (size_t n = 0; n < available; ++n) {

                float w = 0.35875f //blackman-harris
                    - 0.48829f * cosf(2.0f * M_PI * n / (available - 1))
                    + 0.14128f * cosf(4.0f * M_PI * n / (available - 1))
                    - 0.01168f * cosf(6.0f * M_PI * n / (available - 1));

                coherent_gain += w;

                // Apply window and shift to center spectrum (-1)^n
                _liquid_inout[n] *= w * ((n % 2 == 0) ? 1.0f : -1.0f);
            }
            coherent_gain /= static_cast<float>(available);

            // Run FFT
            fft_execute(_fftplan);

            // Normalize bin power by (N * CG)^2
            float scale = static_cast<float>(available) * coherent_gain;
            float scale2 = scale * scale;

            // Fill magnitude buffer in dBFS
            for (size_t j = 0; j < available; ++j) {
                float re = _liquid_inout[j].real();
                float im = _liquid_inout[j].imag();
                float power = (re * re + im * im) / scale2;
                _tmp_magnitude_buffer[j] = 10.0f * log10f(power + 1e-20f); // dBFS
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
        ImGui::Begin(name().c_str());


        const ImPlotAxisFlags x_flags = ImPlotAxisFlags_Lock;
        const ImPlotAxisFlags y_flags = ImPlotAxisFlags_Lock;
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (ImPlot::BeginPlot(_signal_labels[i].c_str())) {
                ImPlot::SetupAxes("Frequency (Hz)", "Time (frames)", x_flags, y_flags);
                ImPlot::SetupAxisLimits(ImAxis_X1, -static_cast<double>(_sps)/2.0, static_cast<double>(_sps)/2.0);
                ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(_tall), 0.0);  // flipped Y! (tall -> 0)
                ImPlot::PushColormap(ImPlotColormap_Plasma);

                std::string label = "##" + std::string(_signal_labels[i]); //ignore label
                ImPlot::PlotHeatmap(
                    label.c_str(),
                    _spectrograms[i],
                    _tall,
                    _buffer_size,
                    0.0, 0.0,
                    nullptr,
                    ImPlotPoint(-static_cast<double>(_sps)/2.0, static_cast<double>(_tall)), //flipped Y! (tall -> 0)
                    ImPlotPoint(static_cast<double>(_sps)/2.0, 0)
                );
                ImPlot::PopColormap();

                if (ImPlot::IsPlotHovered()) {
                    ImPlotPoint mouse = ImPlot::GetPlotMousePos();

                    double freq = mouse.x;
                    double time = mouse.y;

                    size_t freq_idx = static_cast<size_t>(((freq + (_sps / 2.0)) / static_cast<double>(_sps)) * _buffer_size);
                    size_t time_idx = static_cast<size_t>((time / static_cast<double>(_tall)) * _tall);
                    if (freq_idx > _buffer_size - 1) {freq_idx = _buffer_size - 1;}
                    if (time_idx > _tall - 1) {time_idx = _tall - 1;}

                    // Flip Y index because bounds are inverted
                    time_idx = _tall - time_idx - 1;
                    size_t logical_row = _tall - 1 - time_idx;
                    float dbFS = _spectrograms[i][logical_row * _buffer_size + freq_idx];

                    ImGui::BeginTooltip();
                    ImGui::Text("Freq: %.1f Hz", freq);
                    ImGui::Text("Frame: %.0f", time);
                    ImGui::Text("Power: %.1f dB(FS)", dbFS);
                    ImGui::EndTooltip();
                }

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
    std::vector<std::string> _signal_labels;
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
