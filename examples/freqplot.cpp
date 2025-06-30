#include "cler.hpp"
#include "result.hpp"
#include "utils.hpp"
#include <cmath>

#include <thread>
#include <chrono>
#include <GLFW/glfw3.h>


#include "imgui.h"
#include "implot.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

const size_t CHANNEL_SIZE = 512;
const size_t BATCH_SIZE = CHANNEL_SIZE / 2;

struct GuiManager {
    GLFWwindow* window = nullptr;

    void init() {
    if (!glfwInit()) {
        throw std::runtime_error("GLFW init failed!");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window = glfwCreateWindow(800, 400, "DSP Blocks", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glEnable(GL_MULTISAMPLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGui::GetStyle().AntiAliasedLines = true;
    ImGui::GetStyle().AntiAliasedLinesUseTex = true; 
    }

    void begin_frame() {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void end_frame() {
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    bool should_close() {
        return glfwWindowShouldClose(window);
    }

    void shutdown() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

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
            float value = std::sin(phase + i * freq) * 100.0f; // scale to visible amplitude
            out->push(value);
        }

        phase += 0.1f; // shift phase to animate
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
        ImGui::Begin("Frequency Plot");
        if (ImPlot::BeginPlot("Waveform", ImVec2(-1, 300))) {
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

//init window and the rest


int main() {
    GuiManager gui;
    gui.init();

    SourceBlock source("Source");
    FreqPlotBlock freqplot("FreqPlot");

    cler::BlockRunner source_runners{&source, &freqplot.in};
    cler::BlockRunner freqplot_runners{&freqplot};

    cler::FlowGraph flowgraph(
        source_runners,
        freqplot_runners
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        freqplot.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    gui.shutdown();
}
