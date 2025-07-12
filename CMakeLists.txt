cmake_minimum_required(VERSION 3.8)
project(cler
        VERSION 1.0.0
        LANGUAGES CXX C
        DESCRIPTION "Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra -Wno-unused-variable -Wno-missing-braces)

#this is super important for our core ring buffers. can be the difference of 20% throughput
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
add_compile_options(-O3)
# add_compile_options(-g)

add_subdirectory(blocks)
add_subdirectory(examples)