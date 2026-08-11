#include "gui_manager.hpp"
#include "cler_desktop_utils.hpp"

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef CLER_GUI_HAVE_ZLIB
#include <zlib.h>
#endif

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

#ifdef CLER_GUI_HAVE_ZLIB
void put_u32_be(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char>(v & 0xFF);
}

// Write the current framebuffer as an 8-bit truecolor PNG. glReadPixels returns
// rows bottom-up and PNG stores them top-down, so rows are emitted in reverse,
// each prefixed with a filter byte (0 = None). Needs zlib for the IDAT deflate
// stream; write_screenshot() falls back to BMP when zlib was absent at build.
bool write_framebuffer_png(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    const size_t row = static_cast<size_t>(w) * 3;
    std::vector<unsigned char> raw(static_cast<size_t>(h) * (row + 1));
    for (int y = 0; y < h; ++y) {
        unsigned char* dst = raw.data() + static_cast<size_t>(y) * (row + 1);
        dst[0] = 0;                                       // filter: None
        std::memcpy(dst + 1, rgb.data() + static_cast<size_t>(h - 1 - y) * row, row);
    }

    uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), static_cast<uLong>(raw.size()),
                  6) != Z_OK) {
        return false;
    }

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    bool ok = true;
    auto put = [&](const void* p, size_t n) {
        ok = ok && std::fwrite(p, 1, n, f) == n;
    };
    // length + 4-char tag + payload + CRC32 over (tag, payload).
    auto chunk = [&](const char* tag, const unsigned char* d, size_t n) {
        unsigned char be[4];
        put_u32_be(be, static_cast<uint32_t>(n));
        put(be, 4);
        put(tag, 4);
        if (n) put(d, n);
        uLong c = crc32(crc32(0L, Z_NULL, 0),
                        reinterpret_cast<const Bytef*>(tag), 4);
        if (n) c = crc32(c, d, static_cast<uInt>(n));
        put_u32_be(be, static_cast<uint32_t>(c));
        put(be, 4);
    };

    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put(sig, sizeof(sig));
    unsigned char ihdr[13] = {0};
    put_u32_be(ihdr + 0, static_cast<uint32_t>(w));
    put_u32_be(ihdr + 4, static_cast<uint32_t>(h));
    ihdr[8] = 8;    // bit depth
    ihdr[9] = 2;    // color type 2 = truecolor RGB
    chunk("IHDR", ihdr, sizeof(ihdr));
    chunk("IDAT", z.data(), zlen);
    chunk("IEND", nullptr, 0);
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}
#endif  // CLER_GUI_HAVE_ZLIB

bool ends_with_png(const std::string& p) {
    if (p.size() < 4) return false;
    const std::string ext = p.substr(p.size() - 4);
    return ext == ".png" || ext == ".PNG";
}

// Format follows the requested extension: .png (agent/tool friendly) when zlib
// is available, BMP otherwise or for any other extension.
bool write_screenshot(const std::string& path, int w, int h) {
    if (ends_with_png(path)) {
#ifdef CLER_GUI_HAVE_ZLIB
        return write_framebuffer_png(path.c_str(), w, h);
#else
        // Keep the pixels rather than lose the capture: same base name, .bmp.
        const std::string alt = path.substr(0, path.size() - 4) + ".bmp";
        std::fprintf(stderr, "GuiManager: built without zlib (no PNG support); "
                             "wrote %s instead\n", alt.c_str());
        return write_framebuffer_bmp(alt.c_str(), w, h);
#endif
    }
    return write_framebuffer_bmp(path.c_str(), w, h);
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
        if (!write_screenshot(_screenshot_path, display_w, display_h)) {
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
