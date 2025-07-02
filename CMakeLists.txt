cmake_minimum_required(VERSION 3.8)
project(cler
        VERSION 1.0.0
        LANGUAGES CXX C
        DESCRIPTION "a lean dsp block library for sdr")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra -Wno-unused-variable -Wno-missing-braces)

set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

#note: Cler is a header only library, so you dont need to build it to use it.
#it does have sub-libraries that have depedencies
#examples use these sub-libraries, so you need to build them for the examples to work

include_directories(src)

add_subdirectory(src/blocks)
add_subdirectory(src/gui)
add_subdirectory(examples)