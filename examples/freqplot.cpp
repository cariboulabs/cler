#include "cler.hpp"
#include <cmath>
#include "gui/gui_manager.hpp"

const size_t CHANNEL_SIZE = 512;
const size_t BATCH_SIZE = CHANNEL_SIZE / 2;

struct SourceBlock : public cler::BlockBase<SourceBlock> {
    SourceBlock(const char* name) : BlockBase(name) {}

    cler::Result<cler::Empty, ClerError> procedure_impl(
        cler::Channel<float>* out) {

        if (out->space() < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }

        static float phase = 0.0f;
        const float freq = 0.05f;

        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            float value = std::sin(phase + i * freq) * 100.0f;
            out->push(value);
        }

        phase += 0.1f;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
        
        return cler::Empty{};
    }
};
struct FreqPlotBlock : public cler::BlockBase<FreqPlotBlock> {
    cler::Channel<float> in;

    FreqPlotBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE) {
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            _x[i] = static_cast<float>(i);
        }
    }

    cler::Result<cler::Empty, ClerError> procedure_impl() {
        if (in.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }
        in.readN(_samples, BATCH_SIZE);
        return cler::Empty{};
    }

    void render() {
        // Optional: Get main viewport size for centering
        ImVec2 display_size = ImGui::GetIO().DisplaySize;

        // Center the window on first use
        static bool first_open = true;
        if (first_open) {
            ImVec2 window_size = ImVec2(1000, 400); // set your preferred window size
            ImVec2 center_pos = ImVec2(
                (display_size.x - window_size.x) * 0.5f,
                (display_size.y - window_size.y) * 0.5f
            );
            ImGui::SetNextWindowPos(center_pos, ImGuiCond_Once);
            ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
            first_open = false;
        }

        ImGui::Begin("Frequency Plot");
        if (ImPlot::BeginPlot("Waveform")) {
            ImPlot::SetupAxes("Sample Index", "Amplitude");
            ImPlot::PlotLine("Signal", _x, _samples, BATCH_SIZE);
            ImPlot::EndPlot();
        }
        ImGui::End();
}

private:
    float _samples[BATCH_SIZE] = {0};
    float _x[BATCH_SIZE] = {0};
};

int main() {
    cler::GuiManager gui(1000, 400 , "Frequency Plot Example");

    SourceBlock source("Source");
    FreqPlotBlock freqplot("FreqPlot");

    cler::BlockRunner source_runners{&source, &freqplot.in};
    cler::BlockRunner freqplot_runners{&freqplot};

    cler::FlowGraph flowgraph(
        source_runners,
        freqplot_runners
    );

    flowgraph.run();

    //rendering has to happen in the MAIN THREAD
    while (!gui.should_close()) {
        gui.begin_frame();
        freqplot.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
