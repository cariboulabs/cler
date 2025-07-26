#include "cler.hpp"
#include "gui_manager.hpp"
#include <iostream>
#include <emscripten.h>
#include <vector>
#include <cmath>

const size_t CHANNEL_SIZE = 1024;
const size_t PLOT_SIZE = 512;

struct SourceCWBlock : public cler::BlockBase {
    float amplitude;
    float frequency;
    float sample_rate;
    float phase;

    SourceCWBlock(const char* name, float amp, float freq, float fs) 
        : BlockBase(name), amplitude(amp), frequency(freq), sample_rate(fs), phase(0.0f) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<float>* out) {
        size_t available = out->space();
        
        for (size_t i = 0; i < available; ++i) {
            float sample = amplitude * std::sin(phase);
            out->push(sample);
            
            phase += 2.0f * M_PI * frequency / sample_rate;
            if (phase > 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }
        
        return cler::Empty{};
    }
};

struct GainBlock : public cler::BlockBase {
    cler::Channel<float> in;
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), in(CHANNEL_SIZE), gain(gain_value) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<float>* out) {
        size_t transferable = std::min(in.size(), out->space());
        for (size_t i = 0; i < transferable; ++i) {
            float value;
            in.pop(value);
            out->push(value * gain);
        }
        return cler::Empty{};
    }
};

struct PlotSinkBlock : public cler::BlockBase {
    cler::Channel<float> in;
    std::vector<float> plot_data;
    size_t write_idx;

    PlotSinkBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE), write_idx(0) {
        plot_data.resize(PLOT_SIZE, 0.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        while (in.size() > 0) {
            float sample;
            in.pop(sample);
            
            plot_data[write_idx] = sample;
            write_idx = (write_idx + 1) % PLOT_SIZE;
        }
        return cler::Empty{};
    }
    
    const std::vector<float>& get_plot_data() const { return plot_data; }
};

// Global components
SourceCWBlock source("Source", 1.0f, 10.0f, 1000.0f);
GainBlock gain("Gain", 1.0f);
PlotSinkBlock plot_sink("PlotSink");
std::unique_ptr<cler::GuiManager> gui = std::make_unique<cler::GuiManager>(800, 600, "Cler WASM GUI Demo");

bool processing_active = false;

void main_loop() {
    // Begin GUI frame
    gui->begin_frame();
    
    // Control window
    ImGui::Begin("Cler WASM Demo");
    
    ImGui::Text("Signal Generator Controls");
    ImGui::SliderFloat("Frequency", &source.frequency, 1.0f, 50.0f);
    ImGui::SliderFloat("Amplitude", &source.amplitude, 0.1f, 2.0f);
    ImGui::SliderFloat("Gain", &gain.gain, 0.1f, 5.0f);
    
    ImGui::Separator();
    
    if (ImGui::Button(processing_active ? "Stop" : "Start")) {
        processing_active = !processing_active;
    }
    
    ImGui::Text("Status: %s", processing_active ? "Processing" : "Stopped");
    
    ImGui::End();
    
    // Plot window
    ImGui::Begin("Signal Plot");
    
    if (processing_active) {
        // Execute DSP chain
        source.procedure(&gain.in);
        gain.procedure(&plot_sink.in);
        plot_sink.procedure();
    }
    
    // Plot the signal
    const auto& data = plot_sink.get_plot_data();
    ImGui::PlotLines("Signal", data.data(), data.size(), 0, nullptr, -2.0f, 2.0f, ImVec2(0, 200));
    
    ImGui::End();
    
    // End GUI frame
    gui->end_frame();
}

// Functions callable from JavaScript
extern "C" {
    void start_processing() {
        processing_active = true;
        std::cout << "GUI processing started" << std::endl;
    }
    
    void stop_processing() {
        processing_active = false;
        std::cout << "GUI processing stopped" << std::endl;
    }
}

int main() {
    std::cout << "Cler WASM GUI Example Started" << std::endl;
    std::cout << "Interactive signal generator with real-time plotting" << std::endl;
    
    // GUI is automatically initialized via RAII constructor
    // Set up main loop
    emscripten_set_main_loop(main_loop, 60, 1);
    
    return 0;
}