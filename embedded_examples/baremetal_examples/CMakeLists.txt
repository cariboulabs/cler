cmake_minimum_required(VERSION 3.15)
project(CLERBaremetalExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bm_hello_world
    bm_hello_world.cpp
)
target_link_libraries(bm_hello_world cler::cler)

# Optional: Create a test target
add_custom_target(bm_test_compilation 
    DEPENDS bm_hello_world
    COMMENT "Testing bare metal example compilation"
)