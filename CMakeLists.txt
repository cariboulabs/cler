cmake_minimum_required(VERSION 3.16)
project(cler
        VERSION 0.1.0
        LANGUAGES CXX C
        DESCRIPTION "Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
# Set compile options based on compiler
if(MSVC)
  add_compile_options(
    /W3  # Warning level 3
    /wd4996  # Disable deprecated warnings
    /wd4267  # Disable size_t to int conversion warnings
    /wd4244  # Disable narrowing conversion warnings
    /UNDEBUG  # Keep asserts enabled
  )
  add_compile_definitions(NOMINMAX)  # Prevent Windows.h from defining min/max macros
  message(STATUS "MSVC: Disabled min/max macros with NOMINMAX")
else()
  # GCC/Clang options
  add_compile_options(-Wall -Wextra #mainly disable warnings for liquid. but need a better way of this doing
        -Wno-unused-variable 
        -Wno-missing-braces 
        -Wno-unused-function 
        -Wno-unused-parameter
        -Wno-deprecated-declarations
        -Wno-sign-compare
        -Wno-implicit-fallthrough
        -Wno-return-type-c-linkage
        -UNDEBUG
        )
endif()
message(STATUS "Asserts are ENABLED even in Release builds")

option(CLER_BUILD_BLOCKS "Build CLER blocks" ON)
option(CLER_BUILD_BLOCKS_GUI "Build CLER blocks with gui" ON)
option(CLER_BUILD_BLOCKS_LIQUID "Build CLER blocks that use liquid-dsp" ON)
option(CLER_BUILD_EXAMPLES "Build CLER example binaries" ON)
option(CLER_BUILD_PERFORMANCE "Build CLER performance tests" OFF)
option(CLER_BUILD_TESTS "Build CLER unit tests" OFF)

# Runtime configuration options
set(CLER_DEFAULT_MAX_WORKERS "16" CACHE STRING "Maximum number of worker threads for thread pools (embedded=8, desktop=16, server=32+)")
add_definitions(-DCLER_DEFAULT_MAX_WORKERS=${CLER_DEFAULT_MAX_WORKERS}) #not attaching to a library to allow includers

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# For Release, force -O3
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g)
endif()
message(STATUS "CLER: CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}'")

#Its more idomatic to link against cler::cler, 
#but if provide this so that people can just include cler.hpp directly
set(CLER_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(cler INTERFACE)
add_library(cler::cler ALIAS cler)
target_include_directories(cler INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> #allows people to just #include "cler.hpp"
)

# Check if compiler supports -Wno-interference-size (GCC 12+ specific)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-interference-size COMPILER_SUPPORTS_WNO_INTERFERENCE_SIZE)
if(COMPILER_SUPPORTS_WNO_INTERFERENCE_SIZE)
  target_compile_options(cler INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
      -Wno-interference-size
    >
  )
endif()

message(STATUS "CLER: Using CLER_DEFAULT_MAX_WORKERS=${CLER_DEFAULT_MAX_WORKERS}")

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