cmake_minimum_required(VERSION 3.15)
project(CLERThreadXExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)

# ThreadX hello world compilation test
add_executable(thx_hello_world
    hello_world.cpp
)

# Compiler flags
target_compile_options(thx_hello_world PRIVATE 
    -Wall -Wextra -Wpedantic -Wno-interference-size
)

# Link math library
target_link_libraries(thx_hello_world m)

# Optional: Create a test target
add_custom_target(thx_test_compilation 
    DEPENDS thx_hello_world
    COMMENT "Testing ThreadX example compilation"
)