add_executable(streamlined streamlined.cpp)
add_executable(flowgraph flowgraph.cpp)

#--------------------------------FreqPlot--------------------------------
# === Dependencies ===
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)

find_package(Threads REQUIRED)

# OpenGL (modern way)
cmake_policy(SET CMP0072 NEW)
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# === Fetch Dear ImGui ===
include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# === Fetch ImPlot ===
FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(implot)

# === FreqPlot executable ===
add_executable(freqplot
    freqplot.cpp

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

# === Include directories ===
target_include_directories(freqplot PRIVATE
    ${GLFW_INCLUDE_DIRS}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)

# === Link libraries ===
target_link_libraries(freqplot PRIVATE
    glfw  # Link GLFW manually too, in case pkg-config doesn't add it
    OpenGL::GL
    Threads::Threads
    dl  # Needed on Linux for dynamic loading
)

# === Helpful debug messages ===
message(STATUS "GLFW libraries: ${GLFW_LIBRARIES}")