cmake_minimum_required(VERSION 3.15)
project(CLERFreeRTOSExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock_environment)

# FreeRTOS hello world compilation test
add_executable(frts_hello_world
    frts_hello_world.cpp
)
target_link_libraries(frts_hello_world cler::cler Threads::Threads)

# Optional: Create a test target
add_custom_target(frts_test_compilation 
    DEPENDS frts_hello_world
    COMMENT "Testing FreeRTOS example compilation"
)