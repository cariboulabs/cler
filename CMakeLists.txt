cmake_minimum_required(VERSION 3.16)
project(cler
        VERSION 0.1.0
        LANGUAGES CXX C
        DESCRIPTION "Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems")

set(CMAKE_CXX_STANDARD 17)
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
      -UNDEBUG
      )
message(STATUS "Asserts are ENABLED even in Release builds")

option(CLER_BUILD_BLOCKS "Build CLER blocks" ON)
option(CLER_BUILD_BLOCKS_GUI "Build CLER blocks with gui" ON)
option(CLER_BUILD_BLOCKS_LIQUID "Build CLER blocks that use liquid-dsp" ON)
option(CLER_BUILD_EXAMPLES "Build CLER example binaries" ON)
option(CLER_BUILD_PERFORMANCE "Build CLER performance tests" OFF)
option(CLER_BUILD_TESTS "Build CLER unit tests" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

#Its more idomatic to link against cler::cler, 
#but if provide this so that people can just include cler.hpp directly
set(CLER_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

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

target_compile_options(cler INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:
    -Wno-interference-size
  >
)

add_subdirectory(logger)

if (CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_blocks)
endif()

if(CLER_BUILD_EXAMPLES AND CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_examples)
  add_subdirectory(embedded_examples)
endif()

if(CLER_BUILD_PERFORMANCE)
  add_subdirectory(performance)
endif()

if(CLER_BUILD_TESTS)
  add_subdirectory(tests)
endif()