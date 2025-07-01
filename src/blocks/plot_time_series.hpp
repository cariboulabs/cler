#include "cler.hpp"
#include "gui/gui_manager.hpp"

struct PlotTimeSeriesBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    PlotTimeSeriesBlock(const char* name, size_t num_inputs, const char** signal_labels, size_t work_size) : 
        BlockBase(name), _num_inputs(num_inputs), _signal_labels(signal_labels), _work_size(work_size) {
        if (num_inputs < 1) {
            throw std::invalid_argument("PlotTimeSeriesBlock requires at least one input channels");
        }
        if (work_size == 0) {
            throw std::invalid_argument("Work size must be greater than zero.");
        }
        
        // Our ringbuffers are not copy/move so we cant use std::vector
        // As such, we use a raw array of cler::Channel<T>
        // Allocate raw storage only, no default construction
        in = static_cast<cler::Channel<float>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<float>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<float>(2 * work_size);
        }

        // Allocate buffers properly
        _buffers = new float*[num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            _buffers[i] = new float[work_size];
        }
    }

    ~PlotTimeSeriesBlock() {
        delete[] in;
        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _buffers[i];
        }
        delete[] _buffers;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (in[i].size() < _work_size) {
                return cler::Error::NotEnoughSamples;
            }
        }

        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].readN(_buffers[i], _work_size);
        }
    }

    void render() {
        ImGui::Begin("PlotTimeSeries"); //imgui window title
        if (ImPlot::BeginPlot(name())) { //implot title
            ImPlot::SetupAxes("Sample Index", "Amplitude");
            for (size_t i = 0; i < _num_inputs; ++i) {
                ImPlot::PlotLine(_signal_labels[i], _buffers[i], _work_size);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }
    private:
        size_t _num_inputs;
        const char** _signal_labels;
        size_t _work_size;
        float** _buffers;
            
};