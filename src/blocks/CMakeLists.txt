#We add all general block dependencies here. Reminder that core cler does not depend on any of these.
cmake_minimum_required(VERSION 3.16)
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

execute_process(
  COMMAND ldd ${CMAKE_BINARY_DIR}/_deps/liquid-build/libliquid.so
  OUTPUT_VARIABLE LDD_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (LDD_OUTPUT MATCHES "libfftw3f")
  message(STATUS "
  ===============================================================
  ✅  STATUS: libliquid.so is linked to FFTW!
  ===============================================================
  ")
else()
  message(WARNING "
  ===============================================================
  ⚠️  WARNING: libliquid.so is NOT linked to FFTW!
  ===============================================================
  ")
endif()

set(LIQUID_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/_deps/liquid-src/include)
if (NOT EXISTS ${LIQUID_INCLUDE_DIRS})
  message(FATAL_ERROR "
  ===============================================================
  ❌  ERROR: liquid include directory not found at ${LIQUID_INCLUDE_DIRS}!
  Please ensure that liquid is correctly fetched and built.
  ===============================================================
  ")
else()
  message(STATUS "
  ===============================================================
  ✅  STATUS: liquid include directory found at ${LIQUID_INCLUDE_DIRS}!
  ===============================================================
  ")
endif()