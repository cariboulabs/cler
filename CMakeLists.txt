cmake_minimum_required(VERSION 3.8)
project(cler
        VERSION 1.0.0
        LANGUAGES CXX C
        DESCRIPTION "a lean dsp block library for sdr")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra -Wno-unused-variable -Wno-missing-braces)

set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

add_subdirectory(blocks)
add_subdirectory(examples)