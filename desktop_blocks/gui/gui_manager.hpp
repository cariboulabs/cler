#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

//included here so everyone that incldues this header can use ImGui and ImPlot
#include "imgui.h"
#include "implot.h"

#include "cler.hpp"

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

    void request_close();

    void set_frame_sleep_ms(int ms) { _frame_sleep_ms = ms; }
    void frame_sleep() const;

    // Request a one-shot screenshot of the next completed frame. The capture
    // happens inside end_frame() after the UI is drawn but before the buffer
    // swap. A `.png` path writes an 8-bit RGB PNG (needs zlib at build time,
    // else it falls back to the same base name as .bmp); any other extension
    // writes a 24-bit uncompressed BMP. Failures are reported on stderr.
    // GUI-thread only (same thread as end_frame()).
    void request_screenshot(const std::string& path);

private:
    GLFWwindow* window = nullptr;

    std::string _screenshot_path;
    bool        _screenshot_pending = false;
    int         _frame_sleep_ms = 15;
};

namespace gui {

namespace detail {
    template <typename T, typename = void>
    struct has_render : std::false_type {};
    template <typename T>
    struct has_render<T, std::void_t<decltype(std::declval<T&>().render())>>
        : std::true_type {};
    template <typename T>
    constexpr bool has_render_v = has_render<std::remove_reference_t<T>>::value;

    template <typename T, typename = void>
    struct has_post_render : std::false_type {};
    template <typename T>
    struct has_post_render<T, std::void_t<decltype(std::declval<T&>().post_render())>>
        : std::true_type {};
    template <typename T>
    constexpr bool has_post_render_v = has_post_render<std::remove_reference_t<T>>::value;
} // namespace detail

template <typename FG, typename... Extras>
void render_all(FG& fg, Extras*... extras) {
    static_assert(
        ((detail::has_render_v<Extras> || detail::has_post_render_v<Extras>) && ...),
        "every extra must implement render() and/or post_render()");
    ([&] { if constexpr (detail::has_render_v<Extras>) extras->render(); }(), ...);
    fg.for_each_block([](auto& block) {
        if constexpr (cler::block_declares_is_gui_v<decltype(block)>) {
            static_assert(detail::has_render_v<decltype(block)>,
                          "a block declaring is_gui must implement void render()");
            block.render();
        }
    });
    ([&] { if constexpr (detail::has_post_render_v<Extras>) extras->post_render(); }(), ...);
}

template <typename FG, typename... Extras>
void render_loop(GuiManager& gui, FG& fg, Extras*... extras) {
    while (!gui.should_close() && !fg.is_stopped()) {
        gui.begin_frame();
        render_all(fg, extras...);
        gui.end_frame();
        gui.frame_sleep();
    }
}

} // namespace gui

} // namespace cler
