#pragma once
#include "cler.hpp"
#include "gui/gui_manager.hpp"

struct PlotTimeSeriesBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    PlotTimeSeriesBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t sps, float duration_s) 
        : BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _sps(sps) 
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
            size_t available = _x_channel->size();
            _x_channel->readN(_snapshot_x_buffer, available);
            for (size_t i = 0; i < _num_inputs; ++i)
            {
                _y_channels[i].readN(_snapshot_y_buffers[i], available);
            }
            _snapshot_ready.store(available, std::memory_order_release);
        }

        return cler::Empty{};
    }

    void render() {
        _snapshot_ready.store(0, std::memory_order_relaxed);
        _snapshot_requested.store(true, std::memory_order_release);

        ImGui::Begin("PlotTimeSeries");
        ImPlot::SetNextAxesToFit();  
        if (ImPlot::BeginPlot(name())) {
            ImPlot::SetupAxes("Time [s]", "Y");

            size_t available;
            while (available == 0) {
                available = _snapshot_ready.load(std::memory_order_acquire);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for (size_t i = 0; i < _num_inputs; ++i) {
                ImPlot::PlotLine(_signal_labels[i], _snapshot_x_buffer, _snapshot_y_buffers[i], static_cast<int>(available));
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

    cler::Channel<float>* _y_channels;  // ring buffers for each signal
    cler::Channel<float>* _x_channel;   // ring buffer for timestamps

    std::atomic<bool> _snapshot_requested = false;
    std::atomic<size_t> _snapshot_counter = 0;
    std::atomic<size_t> _snapshot_size = 0;
    float* _snapshot_x_buffer = nullptr;
    float** _snapshot_y_buffers = nullptr;

    float* _tmp_y_buffer = nullptr;
    float* _tmp_x_buffer = nullptr;
};
