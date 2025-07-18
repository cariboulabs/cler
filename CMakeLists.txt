cmake_minimum_required(VERSION 3.8)
project(cler
        VERSION 0.1.0
        LANGUAGES CXX C
        DESCRIPTION "Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Wextra #mainly disable warnings for liquid. but need a better way of this doing
      -Wno-unused-variable 
      -Wno-missing-braces 
      -Wno-unused-function 
      -Wno-unused-parameter
      -Wno-deprecated-declarations
      -Wno-sign-compare
      -Wno-implicit-fallthrough
      )

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# For Release, force -O3
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "CLER sources: Configuring Release build with -O3")
  add_compile_options(-O3)
endif()

# For Debug, force debug symbols
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "CLER sources: Configuring Debug build with -g")
  add_compile_options(-g)
endif()

add_library(cler INTERFACE)
add_library(cler::cler ALIAS cler)
target_include_directories(cler INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> #allows people to just #include "cler.hpp"
)

add_subdirectory(logger)
add_subdirectory(blocks)
add_subdirectory(desktop_examples)
add_subdirectory(performance)
