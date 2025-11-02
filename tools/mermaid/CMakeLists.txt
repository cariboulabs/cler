cmake_minimum_required(VERSION 3.15)
project(cler-mermaid VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Fetch tree-sitter
FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.6
)
FetchContent_GetProperties(tree-sitter)
if(NOT tree-sitter_POPULATED)
    FetchContent_Populate(tree-sitter)
    # Build tree-sitter as a static library
    add_library(tree-sitter STATIC
        ${tree-sitter_SOURCE_DIR}/lib/src/lib.c
    )
    target_include_directories(tree-sitter PUBLIC
        $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/include>
    )
endif()

# Fetch tree-sitter-cpp
FetchContent_Declare(
    tree-sitter-cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
)
FetchContent_GetProperties(tree-sitter-cpp)
if(NOT tree-sitter-cpp_POPULATED)
    FetchContent_Populate(tree-sitter-cpp)
    # Build tree-sitter-cpp as a library
    add_library(tree-sitter-cpp STATIC
        ${tree-sitter-cpp_SOURCE_DIR}/src/parser.c
        ${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-cpp PUBLIC
        $<BUILD_INTERFACE:${tree-sitter-cpp_SOURCE_DIR}/src>
    )
    target_link_libraries(tree-sitter-cpp PRIVATE tree-sitter)
endif()

# Main executable
add_executable(cler-mermaid
    src/main.cpp
    src/cpp_parser.cpp
    src/mermaid_renderer.cpp
    src/flowgraph.cpp
)

target_include_directories(cler-mermaid PRIVATE
    $<BUILD_INTERFACE:${tree-sitter_SOURCE_DIR}/lib/include>
)

target_link_libraries(cler-mermaid PRIVATE
    tree-sitter
    tree-sitter-cpp
)

# Install target
install(TARGETS cler-mermaid
    RUNTIME DESTINATION bin
)
