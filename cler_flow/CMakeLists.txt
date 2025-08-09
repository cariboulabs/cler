cmake_minimum_required(VERSION 3.14)
project(cler_flow)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find libclang for parsing C++ headers
# First try to find llvm-config
find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15 llvm-config-14 llvm-config)

if(LLVM_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
        OUTPUT_VARIABLE LLVM_LIBRARY_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
        OUTPUT_VARIABLE LLVM_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
        HINTS ${LLVM_INCLUDE_DIR}
        NO_DEFAULT_PATH
    )
    
    find_library(LIBCLANG_LIBRARY
        NAMES clang
        HINTS ${LLVM_LIBRARY_DIR}
        NO_DEFAULT_PATH
    )
    
    if(LIBCLANG_INCLUDE_DIR AND LIBCLANG_LIBRARY)
        set(LIBCLANG_FOUND TRUE)
        message(STATUS "Found libclang via llvm-config")
        message(STATUS "  Library: ${LIBCLANG_LIBRARY}")
        message(STATUS "  Include: ${LIBCLANG_INCLUDE_DIR}")
    endif()
endif()

# Fallback to manual search if llvm-config didn't work
if(NOT LIBCLANG_FOUND)
    find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
        HINTS
        /usr/lib/llvm-18/include
        /usr/lib/llvm-17/include
        /usr/lib/llvm-16/include
        /usr/lib/llvm-15/include
        /usr/lib/llvm-14/include
        /usr/include/clang-c
        /usr/local/include
    )
    
    find_library(LIBCLANG_LIBRARY
        NAMES clang libclang
        HINTS
        /usr/lib/llvm-18/lib
        /usr/lib/llvm-17/lib
        /usr/lib/llvm-16/lib
        /usr/lib/llvm-15/lib
        /usr/lib/llvm-14/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib
        /usr/local/lib
    )
    
    if(LIBCLANG_INCLUDE_DIR AND LIBCLANG_LIBRARY)
        set(LIBCLANG_FOUND TRUE)
        message(STATUS "Found libclang manually")
        message(STATUS "  Library: ${LIBCLANG_LIBRARY}")
        message(STATUS "  Include: ${LIBCLANG_INCLUDE_DIR}")
    else()
        message(WARNING "libclang not found - block import feature will be disabled")
        set(LIBCLANG_FOUND FALSE)
    endif()
endif()

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
    src/flow_app.cpp
    src/gui_app.cpp
    src/flow_canvas.cpp
    src/visual_node.cpp
    src/block_spec.cpp
    src/block_library.cpp
)

# Add block parser and cache if libclang is available
if(LIBCLANG_FOUND)
    list(APPEND SOURCES src/block_parser.cpp)
    list(APPEND SOURCES src/block_cache.cpp)
    add_compile_definitions(HAS_LIBCLANG)
endif()

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
    
    # Link libclang if found
    if(LIBCLANG_FOUND)
        target_include_directories(${PROJECT_NAME} PRIVATE ${LIBCLANG_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBCLANG_LIBRARY})
    endif()
    
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

# Optional testing
option(CLER_FLOW_BUILD_TESTS "Build the test suite" OFF)

if(CLER_FLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/connections)
    if(LIBCLANG_FOUND)
        add_subdirectory(tests/parser)
    endif()
endif()