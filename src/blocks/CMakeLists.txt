#We add all general block dependencies here. Reminder that core cler does not depend on any of these.
cmake_minimum_required(VERSION 3.16)

include(FetchContent)
include(ExternalProject)

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

# ---- FFTW with ExternalProject_Add ----
# This downloads the official tarball with generated sources!
set(FFTW_INSTALL_DIR ${CMAKE_BINARY_DIR}/_deps/fftw)
ExternalProject_Add(
  fftw
  URL http://fftw.org/fftw-3.3.10.tar.gz
  URL_HASH MD5=8ccbf6a5ea78a16dbc3e1306e234cc5c

  CONFIGURE_COMMAND ./configure --enable-float --prefix=${FFTW_INSTALL_DIR}
  BUILD_COMMAND make
  INSTALL_COMMAND make install

  BUILD_IN_SOURCE 1   # Needed for isolated build trees
)