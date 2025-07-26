# === Dependencies (GLFW, OpenGL, ImGui, ImPlot) ===
find_package(Threads REQUIRED)

# For WASM builds, skip system package detection (Emscripten provides GLFW/OpenGL)
if(NOT CLER_BUILD_WASM_EXAMPLES)
    find_package(PkgConfig REQUIRED)
    pkg_search_module(GLFW REQUIRED glfw3)
    cmake_policy(SET CMP0072 NEW)
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL REQUIRED)
endif()

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG master
)
FetchContent_MakeAvailable(implot)

# === Create static library ===
add_library(blocks_gui STATIC
  gui_manager.cpp
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

add_library(cler::blocks_gui ALIAS blocks_gui)

# === Include paths for anyone linking blocks_gui ===
target_include_directories(blocks_gui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${implot_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add system GLFW includes only for non-WASM builds
if(NOT CLER_BUILD_WASM_EXAMPLES)
  target_include_directories(blocks_gui PUBLIC ${GLFW_INCLUDE_DIRS})
endif()

# === Link dependencies ===
target_link_libraries(blocks_gui PUBLIC
  Threads::Threads
)

# Link system libraries only for non-WASM builds (Emscripten provides GLFW/OpenGL)
if(NOT CLER_BUILD_WASM_EXAMPLES)
  target_link_libraries(blocks_gui PUBLIC
    glfw
    OpenGL::GL
  )
  
  if(UNIX AND NOT APPLE)
    target_link_libraries(blocks_gui PUBLIC dl)
  endif()
else()
  # For WASM builds, enable pthread support for shared memory
  target_compile_options(blocks_gui PUBLIC -pthread)
  target_link_options(blocks_gui PUBLIC -pthread)
endif()

# Optional: Show status
message(STATUS "Built blocks_gui library with GLFW: ${GLFW_LIBRARIES}")
