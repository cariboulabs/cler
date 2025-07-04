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

        _tmp_x_buffer = new float[_buffer_size];
        _tmp_y_buffer = new float[_buffer_size];
    }

    ~PlotTimeSeriesBlock() {
        for (size_t i = 0; i < _num_inputs; ++i) {
           delete &in[i];
           delete &_y_channels[i];
        }
        ::operator delete[](in);
        ::operator delete[](_y_channels);

        delete _x_channel;
        
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

        size_t commit_read_size = (_x_channel->size() >= work_size) ? work_size : 0;

        // Read & push to y buffers
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].readN(_tmp_y_buffer, work_size);
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

        return cler::Empty{};
    }

    void render() {
        ImGui::Begin("PlotTimeSeries");
        if (ImPlot::BeginPlot(name())) {
            ImPlot::SetupAxes("Time [s]", "Y");

            const float* x_ptr1 = nullptr;
            const float* x_ptr2 = nullptr;
            std::size_t x_s1 = 0, x_s2 = 0;
            _x_channel->peek_read(x_ptr1, x_s1, x_ptr2, x_s2);
            memcpy(_tmp_x_buffer, x_ptr1, x_s1 * sizeof(float));
            memcpy(_tmp_x_buffer + x_s1, x_ptr2, x_s2 * sizeof(float));

            for (size_t i = 0; i < _num_inputs; ++i) {
                const float* y_ptr1 = nullptr;
                const float* y_ptr2 = nullptr;
                std::size_t y_s1 = 0, y_s2 = 0;
                _y_channels[i].peek_read(y_ptr1, y_s1, y_ptr2, y_s2);
                memcpy(_tmp_y_buffer, y_ptr1, y_s1 * sizeof(float));
                memcpy(_tmp_y_buffer + y_s1, y_ptr2, y_s2 * sizeof(float));

                ImPlot::PlotLine(_signal_labels[i], _tmp_x_buffer, _tmp_y_buffer, static_cast<int>(x_s1 + x_s2));
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

    float* _tmp_y_buffer;
    float* _tmp_x_buffer;

    cler::Channel<float>* _y_channels;  // ring buffers for each signal
    cler::Channel<float>* _x_channel;   // ring buffer for timestamps
};
