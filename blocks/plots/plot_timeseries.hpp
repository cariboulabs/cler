#pragma once
#include "cler.hpp"
#include <vector>
#include "imgui.h"

struct PlotTimeSeriesBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    PlotTimeSeriesBlock(std::string name, std::vector<std::string> signal_labels,
        const size_t sps, const float duration_s);
    ~PlotTimeSeriesBlock();
    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

private:
    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
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
