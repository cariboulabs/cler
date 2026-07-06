#pragma once

#include <GLFW/glfw3.h>
#include <string>
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

    // Request a one-shot screenshot of the next completed frame. The capture
    // happens inside end_frame() after the UI is drawn but before the buffer
    // swap, and is written as a 24-bit uncompressed BMP to `path`. Failures
    // are reported on stderr. GUI-thread only (same thread as end_frame()).
    void request_screenshot(const std::string& path);

private:
    GLFWwindow* window = nullptr;

    std::string _screenshot_path;
    bool        _screenshot_pending = false;
};

} // namespace cler
