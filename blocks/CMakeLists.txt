cmake_minimum_required(VERSION 3.16)

add_subdirectory(gui)

include(FetchContent)

# Override options before making liquid available... doesnt work?
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_AUTOTESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  liquid
  GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp
  GIT_TAG master
)
FetchContent_MakeAvailable(liquid)