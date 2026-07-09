#include "gui_manager.hpp"
#include "cler_desktop_utils.hpp"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace cler {

namespace {

void put_u16(unsigned char* p, uint16_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}
void put_u32(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}

// Write the current framebuffer as a 24-bit uncompressed BMP. glReadPixels
// returns rows bottom-up and BMP stores rows bottom-up, so no vertical flip is
// needed -- only an RGB->BGR swizzle and 4-byte row padding. The headers are
// assembled byte-by-byte (little-endian) to avoid struct-packing pitfalls.
bool write_framebuffer_bmp(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    const uint32_t row_stride = (static_cast<uint32_t>(w) * 3u + 3u) & ~3u;
    const uint32_t data_size  = row_stride * static_cast<uint32_t>(h);
    const uint32_t hdr_size   = 14 + 40;   // BITMAPFILEHEADER + BITMAPINFOHEADER

    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    put_u32(hdr + 2,  hdr_size + data_size);              // file size
    put_u32(hdr + 10, hdr_size);                          // pixel data offset
    put_u32(hdr + 14, 40);                                // info header size
    put_u32(hdr + 18, static_cast<uint32_t>(w));
    put_u32(hdr + 22, static_cast<uint32_t>(h));          // positive = bottom-up
    put_u16(hdr + 26, 1);                                 // planes
    put_u16(hdr + 28, 24);                                // bits per pixel
    put_u32(hdr + 34, data_size);                         // image size

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    bool ok = std::fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    std::vector<unsigned char> row(row_stride, 0);
    for (int y = 0; ok && y < h; ++y) {
        const unsigned char* src = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {                     // RGB -> BGR
            row[x * 3 + 0] = src[x * 3 + 2];
            row[x * 3 + 1] = src[x * 3 + 1];
            row[x * 3 + 2] = src[x * 3 + 0];
        }
        ok = std::fwrite(row.data(), 1, row_stride, f) == row_stride;
    }
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

} // namespace

GuiManager::GuiManager(int width, int height, const std::string_view title) {
    if (!glfwInit()) {
        cler::panic("GLFW init failed!");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
    if (!window) {
        cler::panic("Failed to create GLFW window");
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

GuiManager::~GuiManager() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void GuiManager::begin_frame() {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GuiManager::end_frame() {
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (_screenshot_pending) {
        if (!write_framebuffer_bmp(_screenshot_path.c_str(), display_w, display_h)) {
            std::fprintf(stderr, "GuiManager: failed to write screenshot to %s\n",
                         _screenshot_path.c_str());
        }
        _screenshot_pending = false;
    }
    glfwSwapBuffers(window);
}

void GuiManager::request_screenshot(const std::string& path) {
    _screenshot_path    = path;
    _screenshot_pending = true;
}

bool GuiManager::should_close() const {
    return glfwWindowShouldClose(window);
}

} // namespace cler
