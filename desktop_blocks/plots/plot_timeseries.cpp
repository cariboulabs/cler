#include "plot_timeseries.hpp"
#include "implot.h"
#include <cstring>
#include <stdexcept>

PlotTimeSeriesBlock::PlotTimeSeriesBlock(const char* name,
    const std::vector<std::string> signal_labels,
    const size_t sps,
    const float duration_s)
    : BlockBase(name),
      _num_inputs(signal_labels.size()),
      _signal_labels(signal_labels),
      _sps(sps)
{
    if (_num_inputs < 1) {
        throw std::invalid_argument("PlotTimeSeriesBlock requires at least one input channel");
    }
    if (duration_s <= 0) {
        throw std::invalid_argument("Duration must be greater than zero.");
    }

    _buffer_size = static_cast<size_t>(sps * duration_s);

    // Input channels
    in = static_cast<cler::Channel<float>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<float>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<float>(_buffer_size);
    }

    // Output ring buffers for Y and X
    _y_channels = static_cast<cler::Channel<float>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<float>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&_y_channels[i]) cler::Channel<float>(_buffer_size);
    }

    _x_channel = new cler::Channel<float>(_buffer_size);

    // Snapshots
    _snapshot_x_buffer = new float[_buffer_size];
    _snapshot_y_buffers = new float*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _snapshot_y_buffers[i] = new float[_buffer_size];
    }

    // Temp buffers
    _tmp_y_buffer = new float[_buffer_size];
    _tmp_x_buffer = new float[_buffer_size];
}

PlotTimeSeriesBlock::~PlotTimeSeriesBlock() {
    using FloatChannel = cler::Channel<float>;
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

    delete[] _tmp_y_buffer;
    delete[] _tmp_x_buffer;
}

cler::Result<cler::Empty, cler::Error> PlotTimeSeriesBlock::procedure() {
    if (_gui_pause.load(std::memory_order_acquire)) {
        return cler::Empty{};
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

    size_t commit_read_size = (_x_channel->size() + work_size > _buffer_size)
        ? (_x_channel->size() + work_size - _buffer_size) : 0;

    // Process each input channel
    for (size_t i = 0; i < _num_inputs; ++i) {
        // Try zero-copy path first
        auto [read_ptr, read_size] = in[i].read_dbf();
        if (read_ptr && read_size >= work_size) {
            // Try to write directly to internal buffer
            auto [write_ptr, write_size] = _y_channels[i].write_dbf();
            if (write_ptr && write_size >= work_size) {
                // FAST PATH: Direct copy from input to internal buffer
                _y_channels[i].commit_read(commit_read_size);
                std::memcpy(write_ptr, read_ptr, work_size * sizeof(float));
                _y_channels[i].commit_write(work_size);
                in[i].commit_read(work_size);
            } else {
                // Input has dbf but output doesn't
                _y_channels[i].commit_read(commit_read_size);
                _y_channels[i].writeN(read_ptr, work_size);
                in[i].commit_read(work_size);
            }
        } else {
            // Fall back to standard approach
            in[i].readN(_tmp_y_buffer, work_size);
            _y_channels[i].commit_read(commit_read_size);
            _y_channels[i].writeN(_tmp_y_buffer, work_size);
        }
    }

    // Handle X channel (timestamps) - try dbf first
    _x_channel->commit_read(commit_read_size);
    auto [x_write_ptr, x_write_size] = _x_channel->write_dbf();
    if (x_write_ptr && x_write_size >= work_size) {
        // Generate timestamps directly into buffer
        for (size_t i = 0; i < work_size; ++i) {
            x_write_ptr[i] = static_cast<float>(_samples_counter + i) / _sps;
        }
        _x_channel->commit_write(work_size);
    } else {
        // Fall back to push
        for (size_t i = 0; i < work_size; ++i) {
            float t = static_cast<float>(_samples_counter + i) / _sps;
            _x_channel->push(t);
        }
    }
    _samples_counter += work_size;

    return cler::Empty{};
}

void PlotTimeSeriesBlock::render() {
    if (_snapshot_mutex.try_lock()) {
        // Try to update snapshot if no one is writing
        const float* ptr1; const float* ptr2;
        size_t size1, size2;

        size_t available = _x_channel->peek_read(ptr1, size1, ptr2, size2);

        for (size_t i = 0; i < _num_inputs; ++i) {
            size_t a = _y_channels[i].size();
            if (a < available) {
                available = a;
            }
        }

        if (available > 0) {
            _x_channel->peek_read(ptr1, size1, ptr2, size2);
            memcpy(_snapshot_x_buffer, ptr1, size1 * sizeof(float));
            memcpy(_snapshot_x_buffer + size1, ptr2, size2 * sizeof(float));

            for (size_t i = 0; i < _num_inputs; ++i) {
                _y_channels[i].peek_read(ptr1, size1, ptr2, size2);
                memcpy(_snapshot_y_buffers[i], ptr1, size1 * sizeof(float));
                memcpy(_snapshot_y_buffers[i] + size1, ptr2, size2 * sizeof(float));
            }

            _snapshot_ready_size = available;
        }

        _snapshot_mutex.unlock();
    }

    if (_snapshot_ready_size == 0) {
        return;  // Nothing to draw yet
    }

    size_t available = _snapshot_ready_size;

    ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    ImGui::Begin(name());

    if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
        _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
    }

    if (ImPlot::BeginPlot(name())) {
        ImPlot::SetupAxis(ImAxis_X1, "Time [s]", ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_AutoFit);

        for (size_t i = 0; i < _num_inputs; ++i) {
            ImPlot::PlotLine(_signal_labels[i].c_str(),
                _snapshot_x_buffer,
                _snapshot_y_buffers[i],
                static_cast<int>(available));
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

void PlotTimeSeriesBlock::set_initial_window(float x, float y, float w, float h) {
    _initial_window_position = ImVec2(x, y);
    _initial_window_size = ImVec2(w, h);
}
