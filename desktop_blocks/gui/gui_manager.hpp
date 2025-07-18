#pragma once

#include <GLFW/glfw3.h>
#include <string_view>
#include <stdexcept>

//included here so everyone that incldues this header can use ImGui and ImPlot
#include "imgui.h"
#include "implot.h"

namespace cler {

class GuiManager {
public:
    GuiManager(int width = 800, int height = 400, std::string_view title = "DSP Blocks");

    GuiManager(const GuiManager&) = delete; // Copy constructor is deleted
    GuiManager& operator=(const GuiManager&) = delete; // Copy assignment operator is deleted

    ~GuiManager();

    void begin_frame();
    void end_frame();
    bool should_close() const;

private:
    GLFWwindow* window = nullptr;
};

} // namespace cler
