cmake_minimum_required(VERSION 3.16)
project(cler
        VERSION 0.1.0
        LANGUAGES CXX C
        DESCRIPTION "Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems")

include(cmake/CleanOnGenMismatch.cmake)
file(GLOB _dep_builds LIST_DIRECTORIES true
     "${CMAKE_BINARY_DIR}/_deps/*-subbuild" "${CMAKE_BINARY_DIR}/_deps/*-build")
foreach(_d IN LISTS _dep_builds)
  clean_on_gen_mismatch("${_d}")
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
# Set compile options
add_compile_options(-Wall -Wextra
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
message(STATUS "Asserts are ENABLED even in Release builds")

# Built as a subproject (add_subdirectory from a consumer), the optional pieces
# default OFF so a consumer compiles only what it links; opt in by setting them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CLER_STANDALONE ON)
else()
  set(CLER_STANDALONE OFF)
  message(STATUS "cler as a subproject: gui, web blocks and examples default OFF")
endif()
option(CLER_BUILD_BLOCKS "Build CLER blocks" ON)
option(CLER_BUILD_BLOCKS_GUI "Build CLER blocks with gui" ${CLER_STANDALONE})
option(CLER_BUILD_BLOCKS_LIQUID "Build CLER blocks that use liquid-dsp" ON)
option(CLER_BUILD_BLOCKS_WEB "Build CLER blocks that serve a browser client" ${CLER_STANDALONE})
option(CLER_BUILD_EXAMPLES "Build CLER example binaries" ${CLER_STANDALONE})
option(CLER_BUILD_PERFORMANCE "Build CLER performance tests" OFF)
option(CLER_BUILD_TESTS "Build CLER unit tests" OFF)
set(CLER_EDITOR_TARGET "" CACHE STRING "Existing target to link from editor block requirements")
set(CLER_EDITOR_SOURCE "" CACHE FILEPATH "Draft source outside the tree to build as CLER_EDITOR_TARGET")
set(CLER_EDITOR_REQUIREMENTS_FILE "" CACHE FILEPATH "Newline-delimited BlockSpec origin paths for CLER_EDITOR_TARGET")
set(CLER_EDITOR_REQUIREMENTS_EXACT OFF CACHE BOOL "Whether editor requirements cover every desktop block in the draft")

# Default max workers per platform (if not overridden)
if(NOT DEFINED CLER_DEFAULT_MAX_WORKERS)
  if(APPLE)
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
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
endif()

# For Release, force optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)  # Maximum optimization
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g)  # Debug symbols
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
find_package(Threads REQUIRED)
target_link_libraries(cler INTERFACE Threads::Threads)

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

find_package(PkgConfig REQUIRED)

message(STATUS "CLER: Using CLER_DEFAULT_MAX_WORKERS=${CLER_DEFAULT_MAX_WORKERS}")

include(cmake/ClerBlockComponents.cmake)

if (CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_logger) #mightaswell
  add_subdirectory(desktop_blocks)
endif()

if(CLER_BUILD_EXAMPLES AND CLER_BUILD_BLOCKS)
  add_subdirectory(desktop_examples)
  add_subdirectory(embedded_examples)
endif()

if(CLER_EDITOR_TARGET)
  cler_configure_editor_target()
endif()

if(CLER_BUILD_PERFORMANCE)
  add_subdirectory(performance)
endif()

if(CLER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
