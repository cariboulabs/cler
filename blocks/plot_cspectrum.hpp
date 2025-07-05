#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "liquid.h"
#include <complex>

struct PlotCSpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t sps, size_t buffer_size) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps) 
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
            _freq_bins[i] = i * (static_cast<float>(_sps) / static_cast<float>(buffer_size));
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

        _gui_prev_window_size.x = 800.0f;
        _gui_prev_window_size.y = 400.0f;
        float offset_x = static_cast<float>(rand() % static_cast<int>(800.0f / 2));
        float offset_y = static_cast<float>(rand() % static_cast<int>(400.0f / 2));
        _gui_prev_window_pos = ImVec2(offset_x, offset_y);
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

        _samples_counter += work_size;

        if (_snapshot_requested.load(std::memory_order_acquire)) {
            _snapshot_ready_size.store(0, std::memory_order_release); //reset snapshot ready size
            const std::complex<float>* ptr1, *ptr2;
            size_t size1, size2;

            size_t available; //all the same by design
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

        ImGui::SetNextWindowPos(_gui_prev_window_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(_gui_prev_window_size, ImGuiCond_FirstUseEver);

        size_t available = _snapshot_ready_size.load(std::memory_order_acquire);
        if (available < _buffer_size) {
            return; // not enough data to render
        }

        // Optional: Save current window flags
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
        if (_gui_fullscreen) {
            window_flags |= ImGuiWindowFlags_NoTitleBar 
                        | ImGuiWindowFlags_NoResize 
                        | ImGuiWindowFlags_NoMove 
                        | ImGuiWindowFlags_NoCollapse;
            
            // Set the next window position and size to cover the entire viewport
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
        } else if (_gui_just_exited_fullscreen) {
            // Apply the saved size/pos only once right after exiting fullscreen
            ImGui::SetNextWindowPos(_gui_prev_window_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(_gui_prev_window_size, ImGuiCond_Always);
            _gui_just_exited_fullscreen = false;  // Clear the flag so it doesn't repeat
        }
        
        ImGui::Begin(name(), nullptr, window_flags);

        //buttons and stuff
        if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
            _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit Axes", &_gui_auto_fit);
        if (_gui_auto_fit) {
            ImPlot::SetNextAxesToFit();
        }
        ImGui::SameLine();
        if (ImGui::Button(_gui_fullscreen ? "Exit Fullscreen" : "Fullscreen")) {
            if (!_gui_fullscreen) {
            _gui_prev_window_pos = ImGui::GetWindowPos();
            _gui_prev_window_size = ImGui::GetWindowSize();
            } else {
            _gui_just_exited_fullscreen = true;
            }
            _gui_fullscreen = !_gui_fullscreen;
        }
        //end buttons

        if (ImPlot::BeginPlot(name())) {
            ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");

            for (size_t i = 0; i < _num_inputs; ++i) {
                mempcpy(_liquid_inout, _snapshot_y_buffers[i], available * sizeof(liquid_float_complex));
                fft_execute(_fftplan);
                for (size_t j = 0; j < available; ++j) {
                    float re = _liquid_inout[j].real();
                    float im = _liquid_inout[j].imag();
                    _tmp_magnitude_buffer[j] = 10.0f * log10f(sqrtf(re * re + im * im) + 1e-15f);
                }
                ImPlot::PlotLine(_signal_labels[i], _freq_bins, _tmp_magnitude_buffer, _buffer_size);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    size_t _samples_counter = 0;

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

    bool _gui_fullscreen = false;
    bool _gui_just_exited_fullscreen = false; 
    bool _gui_auto_fit = true; // Automatically fit axes to data
    std::atomic<bool> _gui_pause = false;
    ImVec2 _gui_prev_window_pos;
    ImVec2 _gui_prev_window_size;
};
