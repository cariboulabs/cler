cmake_minimum_required(VERSION 3.8)
project(cler2
        VERSION 1.0.0
        LANGUAGES CXX C
        DESCRIPTION "a lean dsp block library for sdr")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra -Wno-unused-variable -Wno-missing-braces)

#-----------Add Liquid------------
include(FetchContent)
FetchContent_Declare(
  liquid
  GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp
  GIT_TAG master
)
FetchContent_MakeAvailable(liquid)
#----------------------------------

include_directories(src)
add_subdirectory(examples)
add_subdirectory(src/gui)