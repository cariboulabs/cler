cmake_minimum_required(VERSION 3.15)
project(CLERZephyrExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)

# Zephyr hello world compilation test
add_executable(zph_hello_world
    hello_world.cpp
)

# Compiler flags
target_compile_options(zph_hello_world PRIVATE 
    -Wall -Wextra -Wpedantic -Wno-interference-size
)

# Link math library
target_link_libraries(zph_hello_world m)

# Optional: Create a test target
add_custom_target(zph_test_compilation 
    DEPENDS zph_hello_world
    COMMENT "Testing Zephyr example compilation"
)