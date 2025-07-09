#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"

struct PlotTimeSeriesBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    PlotTimeSeriesBlock(std::string name, const size_t num_inputs, const char** signal_labels,
        const size_t sps, const float duration_s) 
        : BlockBase(std::move(name)), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps) 
    {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotTimeSeriesBlock requires at least one input channel");
        }
        if (duration_s <= 0) {
            throw std::invalid_argument("Duration must be greater than zero.");
        }

        _buffer_size = static_cast<size_t>(sps * duration_s);

        // Allocate input channels
        in = static_cast<cler::Channel<float>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<float>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<float>(_buffer_size);
        }

        // Allocate y buffers as channels
        _y_channels = static_cast<cler::Channel<float>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<float>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&_y_channels[i]) cler::Channel<float>(_buffer_size);
        }
        // X buffer as channel too
        _x_channel = new cler::Channel<float>(_buffer_size);

        //allocate snapshot buffers
        _snapshot_x_buffer = new float[_buffer_size];
        _snapshot_y_buffers = new float*[_num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _snapshot_y_buffers[i] = new float[_buffer_size];
        }

        _tmp_x_buffer = new float[_buffer_size];
        _tmp_y_buffer = new float[_buffer_size];
    }

    ~PlotTimeSeriesBlock() {
        using FloatChannel = cler::Channel<float>; //cant template on Destructor...
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].~FloatChannel();
            _y_channels[i].~FloatChannel();
        }
        ::operator delete[](in);
        ::operator delete[](_y_channels);

        delete _x_channel;

        delete[] _snapshot_x_buffer;
        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _snapshot_y_buffers[i];
        }
        delete[] _snapshot_y_buffers;
        
        delete[] _tmp_x_buffer;
        delete[] _tmp_y_buffer;
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

        size_t commit_read_size = (_x_channel->size() + work_size) > _buffer_size ?
            _x_channel->size() + work_size - _buffer_size : 0;

        // Read & push to y buffers
        for (size_t i = 0; i < _num_inputs; ++i) {
            size_t read = in[i].readN(_tmp_y_buffer, work_size);
            _y_channels[i].commit_read(commit_read_size);
            _y_channels[i].writeN(_tmp_y_buffer, work_size);
        }

        // Generate x timestamps & push
        _x_channel->commit_read(commit_read_size);
        for (size_t i = 0; i < work_size; ++i) {
            float t = static_cast<float>(_samples_counter + i) / _sps;
            _x_channel->push(t);
        }
        _samples_counter += work_size;

        if (_snapshot_requested.load(std::memory_order_acquire)) {
            _snapshot_ready_size.store(0, std::memory_order_release); //reset snapshot ready size
            const float* ptr1, *ptr2;
            size_t size1, size2;

            size_t available = _x_channel->peek_read(ptr1, size1, ptr2, size2);
            memcpy(_snapshot_x_buffer, ptr1, size1 * sizeof(float));
            memcpy(_snapshot_x_buffer + size1, ptr2, size2 * sizeof(float));
            for (size_t i = 0; i < _num_inputs; ++i) {
                size_t available_y = _y_channels[i].peek_read(ptr1, size1, ptr2, size2);
                assert(available_y == available);
                memcpy(_snapshot_y_buffers[i], ptr1, size1 * sizeof(float));
                memcpy(_snapshot_y_buffers[i] + size1, ptr2, size2 * sizeof(float));
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

        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name().c_str());

        //buttons and stuff
        if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
            _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
        }

        if (ImPlot::BeginPlot(name().c_str())) {
            ImPlot::SetupAxis(ImAxis_X1, "Time [s]", ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_AutoFit);
            

            for (size_t i = 0; i < _num_inputs; ++i) {
                ImPlot::PlotLine(_signal_labels[i], _snapshot_x_buffer, _snapshot_y_buffers[i], static_cast<int>(available));
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
    size_t _samples_counter = 0;

    size_t _num_inputs;
    const char** _signal_labels;
    size_t _sps;
    size_t _buffer_size;

    cler::Channel<float>* _y_channels;  // ring buffers for each signal
    cler::Channel<float>* _x_channel;   // ring buffer for timestamps

    std::atomic<size_t> _snapshot_ready_size = 0;
    std::atomic<bool> _snapshot_requested = false;
    float* _snapshot_x_buffer = nullptr;
    float** _snapshot_y_buffers = nullptr;

    float* _tmp_y_buffer = nullptr;
    float* _tmp_x_buffer = nullptr;

    std::atomic<bool> _gui_pause = false;

    bool _has_initial_window_position = false;
    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};
