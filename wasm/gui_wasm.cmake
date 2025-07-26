# WASM-specific GUI library configuration
# This overrides the desktop GUI library for WASM builds

# === Create WASM GUI library without system dependencies ===
add_library(blocks_gui_wasm STATIC
    ${CMAKE_SOURCE_DIR}/desktop_blocks/gui/gui_manager.cpp
    # ImGui core
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # ImGui backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    # ImPlot core
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

add_library(cler::blocks_gui_wasm ALIAS blocks_gui_wasm)

# === Include paths ===
target_include_directories(blocks_gui_wasm PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks/gui
)

# === Link only threads for WASM (GLFW/OpenGL provided by Emscripten) ===
target_link_libraries(blocks_gui_wasm PUBLIC
    Threads::Threads
)