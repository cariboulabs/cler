#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "liquid.h"

struct PlotCSpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(std::string name, const size_t num_inputs, const char** signal_labels,
        const size_t sps, const size_t buffer_size) 
        : BlockBase(std::move(name)), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps) 
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotCSpectrumBlock requires at least one input channel");
        }
        if (buffer_size <= 2) {
            throw std::invalid_argument("Buffer size must be greater than two.");
        }

        _buffer_size = buffer_size;

        // Allocate input channels
        in = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<std::complex<float>>(_buffer_size);
        }

        // Allocate y buffers as channels
        _y_channels = static_cast<cler::Channel<std::complex<float>>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<std::complex<float>>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&_y_channels[i]) cler::Channel<std::complex<float>>(_buffer_size);
        }

        _freq_bins = new float[buffer_size];
        for (size_t i = 0; i < buffer_size; ++i) {
            _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(buffer_size))) - (_sps / 2.0f);
        }
        //allocate snapshot buffers
        _snapshot_y_buffers = new std::complex<float>*[_num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _snapshot_y_buffers[i] = new std::complex<float>[_buffer_size];
        }

        _tmp_y_buffer = new std::complex<float>[_buffer_size];
        _tmp_magnitude_buffer = new float[_buffer_size];

        _liquid_inout = new std::complex<float>[_buffer_size];
        _fftplan = fft_create_plan(buffer_size, 
            reinterpret_cast<liquid_float_complex*>(_liquid_inout),
            reinterpret_cast<liquid_float_complex*>(_liquid_inout),
             LIQUID_FFT_FORWARD, 0);
    }

    ~PlotCSpectrumBlock() {
        using ComplexChannel = cler::Channel<std::complex<float>>; //cant template on Destructor...
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].~ComplexChannel();
            _y_channels[i].~ComplexChannel();
        }
        ::operator delete[](in);
        ::operator delete[](_y_channels);

        delete[] _freq_bins;

        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _snapshot_y_buffers[i];
        }
        delete[] _snapshot_y_buffers;

        delete[] _liquid_inout;
        fft_destroy_plan(_fftplan);
        
        delete[] _tmp_y_buffer;
        delete[] _tmp_magnitude_buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (_gui_pause.load(std::memory_order_acquire)) {
            return cler::Empty{}; // Do nothing if paused
        }

        size_t work_size = in[0].size();
        for (size_t i = 1; i < _num_inputs; ++i) {
            if (in[i].size() < work_size) {
                work_size = in[i].size();
            }
        }
        if (work_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        work_size = cler::floor2(work_size);

        size_t commit_read_size = (_y_channels[0].size() + work_size) > _buffer_size ?
            _y_channels[0].size() + work_size - _buffer_size : 0;

        // Read & push to y buffers
        for (size_t i = 0; i < _num_inputs; ++i) {
            size_t read = in[i].readN(_tmp_y_buffer, work_size);
            _y_channels[i].commit_read(commit_read_size);
            _y_channels[i].writeN(_tmp_y_buffer, work_size);
        }

        if (_snapshot_requested.load(std::memory_order_acquire)) {
            _snapshot_ready_size.store(0, std::memory_order_release); //reset snapshot ready size
            const std::complex<float>* ptr1, *ptr2;
            size_t size1, size2;

            size_t available = 0; //all the same by design, always <= _buffer_size
            for (size_t i = 0; i < _num_inputs; ++i) {
                available = _y_channels[i].peek_read(ptr1, size1, ptr2, size2);
                memcpy(_snapshot_y_buffers[i], ptr1, size1 * sizeof(std::complex<float>));
                memcpy(_snapshot_y_buffers[i] + size1, ptr2, size2 * sizeof(std::complex<float>));
            }
            _snapshot_ready_size.store(available, std::memory_order_release); //update available samples
            _snapshot_requested.store(false, std::memory_order_release);
        }

        return cler::Empty{};
    }

    void render() {
        _snapshot_requested.store(true, std::memory_order_release);

        if (_snapshot_ready_size.load(std::memory_order_acquire) == 0) {
            return; // nothing to render yet
        }

        size_t available = _snapshot_ready_size.load(std::memory_order_acquire);
        if (available < _buffer_size) {
            return; // not enough data to render
        }
        
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name().c_str());

        //buttons and stuff
        if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
            _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
        }

        if (ImPlot::BeginPlot(name().c_str())) {
            ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");

            for (size_t i = 0; i < _num_inputs; ++i) {
                // Copy snapshot buffer into FFT input
                memcpy(_liquid_inout, _snapshot_y_buffers[i], available * sizeof(liquid_float_complex));

                // Compute coherent gain for Hamming window
                float coherent_gain = 0.0f;
                for (size_t n = 0; n < available; ++n) {
                    float w = 0.54f - 0.46f * cosf(2.0f * M_PI * n / (_buffer_size - 1)); // Hamming
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

                // Plot
                ImPlot::PlotLine(_signal_labels[i], _freq_bins, _tmp_magnitude_buffer, _buffer_size);
            }
            ImPlot::EndPlot();
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
