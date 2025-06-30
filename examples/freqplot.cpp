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

    FreqPlotBlock(const char* name)
        : BlockBase(name), in(CHANNEL_SIZE)
    {
        init_plot();

        // Initialize x-axis values
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            _x[i] = static_cast<float>(i);
        }
    }

    ~FreqPlotBlock() {
        destroy_plot();
    }

    // Runs in MAIN THREAD
    void render() {
        if (glfwWindowShouldClose(_window)) {
            exit(0);
        }

        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Frequency Plot");

        if (ImPlot::BeginPlot("Waveform", ImVec2(-1, 300))) {
            ImPlot::SetupAxes("Sample Index", "Amplitude");
            ImPlot::PlotLine("Signal", _x, _samples, BATCH_SIZE);
            ImPlot::EndPlot();
        }

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(_window);
    }

    // Runs in worker thread
    cler::Result<cler::Empty, ClerError> procedure_impl() {
        if (in.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        // Safely read into the buffer the render() will use
        const size_t read = in.readN(_samples, BATCH_SIZE);
        if (read != BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        return cler::Empty{};
    }

private:
    GLFWwindow* _window = nullptr;
    static constexpr int WINDOW_WIDTH = 800;
    static constexpr int WINDOW_HEIGHT = 400;

    float _samples[BATCH_SIZE] = {0};
    float _x[BATCH_SIZE] = {0};

    void init_plot() {
        if (!glfwInit()) {
            throw std::runtime_error("GLFW init failed!");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        _window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FreqPlot ImPlot", nullptr, nullptr);
        if (!_window) {
            throw std::runtime_error("Failed to create GLFW window");
        }
        glfwMakeContextCurrent(_window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(_window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void destroy_plot() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(_window);
        glfwTerminate();
    }
};

int main() {
    SourceBlock source("Source");
    FreqPlotBlock freqplot("FreqPlot");

    cler::BlockRunner source_runners{&source, &freqplot.in};
    cler::BlockRunner freqplot_runners{&freqplot};

    cler::FlowGraph flowgraph(
        source_runners,
        freqplot_runners
    );

    flowgraph.run();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        freqplot.render();
    }
}
