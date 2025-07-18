cmake_minimum_required(VERSION 3.16)


include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_AUTOTESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  liquid
  GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp
  GIT_TAG master
)
FetchContent_MakeAvailable(liquid)
target_include_directories(liquid INTERFACE 
    SYSTEM ${liquid_SOURCE_DIR}/include #system ignore warnings from liquid
    SYSTEM ${liquid_SOURCE_DIR}/scripts #for config.h
)

FetchContent_Declare(
  zf_log
  GIT_REPOSITORY https://github.com/wonder-mice/zf_log
  GIT_TAG master
)

add_library(cler_desktop_blocks INTERFACE)
add_library(cler::cler_desktop_blocks ALIAS cler_desktop_blocks)

add_subdirectory(gui)
add_subdirectory(ezgmsk_demod)
add_subdirectory(plots)
add_subdirectory(udp)
add_subdirectory(sources)

target_link_libraries(cler_desktop_blocks INTERFACE 
  cler::blocks_gui
  cler::blocks_ezgmsk_demod
  cler::blocks_plots
  cler::blocks_udp
  cler::blocks_sources
)

target_include_directories(cler_desktop_blocks INTERFACE #so people call #include "blocks/..."
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)