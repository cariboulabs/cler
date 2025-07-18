cmake_minimum_required(VERSION 3.15)
project(CLERBaremetalExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
# No mock environment needed for bare metal

# Bare metal hello world compilation test
add_executable(bm_hello_world
    hello_world.cpp
)

# Compiler flags
target_compile_options(bm_hello_world PRIVATE 
    -Wall -Wextra -Wpedantic -Wno-interference-size
)

# Link math library
target_link_libraries(bm_hello_world m)

# Optional: Create a test target
add_custom_target(bm_test_compilation 
    DEPENDS bm_hello_world
    COMMENT "Testing bare metal example compilation"
)