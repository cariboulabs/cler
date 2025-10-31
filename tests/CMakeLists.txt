# Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

message(STATUS "Building CLER Tests")

# Find required packages
find_package(Threads REQUIRED)

# Download and configure Google Test (centralized for all tests)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

# Google Test configuration
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing for this directory and subdirectories
enable_testing()

# Add test subdirectories
add_subdirectory(spsc-queue)
add_subdirectory(desktop_blocks)