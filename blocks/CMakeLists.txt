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
# FetchContent_MakeAvailable(zf_log)
# target_include_directories(zf_log PUBLIC
#   ${zf_log_SOURCE_DIR}/zf_log #system ignore warnings from zf_log
# )

add_library(cler_blocks INTERFACE)

add_subdirectory(gui)
add_subdirectory(ezgmsk_demod)
add_subdirectory(plots)
add_subdirectory(udp)

target_link_libraries(cler_blocks INTERFACE 
  blocks_gui
  blocks_ezgmsk_demod
  blocks_plots
  blocks_udp
)

target_include_directories(cler_blocks INTERFACE #so people call #include "blocks/..."
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)