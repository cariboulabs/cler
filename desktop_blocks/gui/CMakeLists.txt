find_package(PkgConfig REQUIRED)
include(FetchContent)

pkg_check_modules(GLFW glfw3)
if (GLFW_FOUND)
  message(STATUS "Found system GLFW: ${GLFW_LIBRARIES}")
else()
  message(WARNING "System GLFW not found, falling back to FetchContent")

  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
  )
  FetchContent_MakeAvailable(glfw)

  set(GLFW_LIBRARIES glfw)
  set(GLFW_INCLUDE_DIRS ${glfw_SOURCE_DIR}/include)
endif()

find_package(Threads REQUIRED)
cmake_policy(SET CMP0072 NEW)
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

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
  ${GLFW_INCLUDE_DIRS}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${implot_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# === Link dependencies ===
target_link_libraries(blocks_gui PUBLIC
  ${GLFW_LIBRARIES}
  OpenGL::GL
  Threads::Threads
)

# Add GLFW library directories for systems like macOS
target_link_directories(blocks_gui PUBLIC ${GLFW_LIBRARY_DIRS})

if(UNIX AND NOT APPLE)
  target_link_libraries(blocks_gui PUBLIC dl)
endif()

# Optional: Show status
message(STATUS "Built blocks_gui library with GLFW: ${GLFW_LIBRARIES}")
