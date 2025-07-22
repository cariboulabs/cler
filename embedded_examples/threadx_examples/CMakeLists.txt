cmake_minimum_required(VERSION 3.15)
project(CLERThreadXExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)
# ThreadX hello world compilation test
add_executable(thx_hello_world
    thx_hello_world.cpp
)
target_link_libraries(thx_hello_world cler::cler Threads::Threads)

# Optional: Create a test target
add_custom_target(thx_test_compilation 
    DEPENDS thx_hello_world
    COMMENT "Testing ThreadX example compilation"
)