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
    /wd4848  # Disable [[no_unique_address]] warning in C++17
  )
  # Keep assertions enabled
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_DEBUG)
  endif()
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

if(WIN32 AND CLER_BUILD_BLOCKS_LIQUID)
  set(CLER_BUILD_BLOCKS_LIQUID OFF CACHE BOOL "Liquid blocks are not supported on Windows yet" FORCE)
  message(STATUS "CLER: liquid-dsp blocks are disabled on Windows")
endif()

# Default max workers per platform (if not overridden)
if(NOT DEFINED CLER_DEFAULT_MAX_WORKERS)
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CLER_DEFAULT_MAX_WORKERS "16" CACHE STRING "Default: 16 for Windows")
  elseif(APPLE)
    set(CLER_DEFAULT_MAX_WORKERS "16" CACHE STRING "Default: 16 for macOS")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Try to detect embedded via processor type
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|riscv")
      set(CLER_DEFAULT_MAX_WORKERS "8" CACHE STRING "Default: 8 for embedded Linux")
    else()
      set(CLER_DEFAULT_MAX_WORKERS "16" CACHE STRING "Default: 16 for desktop Linux")
    endif()
  else()
    set(CLER_DEFAULT_MAX_WORKERS "8" CACHE STRING "Default: 8 (unknown system)")
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# For Release, force optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    add_compile_options(/O2)  # MSVC optimization for speed
  else()
    add_compile_options(-O3)  # GCC/Clang maximum optimization
  endif()
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(NOT MSVC)
    add_compile_options(-g)  # Debug symbols for GCC/Clang
  endif()
endif()
message(STATUS "CLER: CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}'")

#Its more idomatic to link against cler::cler, 
#but if provide this so that people can just include cler.hpp directly
set(CLER_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(cler INTERFACE)
add_library(cler::cler ALIAS cler)
target_include_directories(cler INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include #allows people to just #include "cler.hpp"
)
if(UNIX AND NOT APPLE)
    target_link_libraries(cler INTERFACE rt)
endif()
if(UNIX OR APPLE OR WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(cler INTERFACE Threads::Threads)
endif()

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

if(WIN32)
  set(PKGCONFIG_EXE ${CMAKE_CURRENT_SOURCE_DIR}/misc/pkg-config.exe)

  if(NOT EXISTS "${PKGCONFIG_EXE}")
    message(FATAL_ERROR "pkg-config.exe not found at ${PKGCONFIG_EXE}")
  endif()

  set(ENV{PKG_CONFIG} "${PKGCONFIG_EXE}")  # Helps subprocesses like pkg_search_module
  set(PKG_CONFIG_EXECUTABLE "${PKGCONFIG_EXE}" CACHE FILEPATH "Path to pkg-config.exe")


  message(STATUS "Using pkg-config: ${PKGCONFIG_EXE}")
endif()

find_package(PkgConfig REQUIRED)

message(STATUS "CLER: Using CLER_DEFAULT_MAX_WORKERS=${CLER_DEFAULT_MAX_WORKERS}")

if (CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_logger) #mightaswell
  add_subdirectory(desktop_blocks)
endif()

if(CLER_BUILD_EXAMPLES AND CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_examples)
  if (NOT WIN32)
    add_subdirectory(embedded_examples)
  else()
    message(STATUS "Skipping embedded examples for Windows builds as they are not supported")
  endif()
endif()

if(CLER_BUILD_PERFORMANCE)
  add_subdirectory(performance)
endif()

if(CLER_BUILD_TESTS)
  add_subdirectory(tests)
endif()