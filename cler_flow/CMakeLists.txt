cmake_minimum_required(VERSION 3.14)
project(cler_flow)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build standalone or use CLER's ImGui
option(CLER_FLOW_STANDALONE "Build as standalone application" ON)

if(CLER_FLOW_STANDALONE)
    # Fetch dependencies when building standalone
    include(FetchContent)
    
    # Fetch ImGui - use specific docking commit like core-nodes
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking  # Using docking branch for docking support
    )
    FetchContent_MakeAvailable(imgui)
    
    # Find required packages
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    find_package(Threads REQUIRED)
    
    # ImGui sources
    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
endif()

# All source files in src directory
set(SOURCES
    src/main.cpp
    src/FlowApp.cpp
    src/GuiApp.cpp
    src/FlowCanvas.cpp
    src/VisualNode.cpp
    src/BlockSpec.cpp
    src/BlockLibrary.cpp
)

# Create executable
if(CLER_FLOW_STANDALONE)
    add_executable(${PROJECT_NAME} ${SOURCES} ${IMGUI_SOURCES})
    
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        OpenGL::GL
        glfw
        Threads::Threads
    )
    
    # Platform-specific settings
    if(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
    
    if(UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE dl)
    endif()
else()
    # When integrated with CLER, use CLER's ImGui
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
    
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    # Link with CLER's ImGui (assumed to be available)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        imgui  # Provided by parent project
        Threads::Threads
    )
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
endif()