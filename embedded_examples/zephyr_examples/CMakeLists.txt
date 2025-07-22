cmake_minimum_required(VERSION 3.15)
project(CLERZephyrExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)
# Zephyr hello world compilation test
add_executable(zph_hello_world
    zph_hello_world.cpp
)
target_link_libraries(zph_hello_world cler::cler Threads::Threads)

# Optional: Create a test target
add_custom_target(zph_test_compilation 
    DEPENDS zph_hello_world
    COMMENT "Testing Zephyr example compilation"
)