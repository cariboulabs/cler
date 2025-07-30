# SPSC Queue Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Find required packages
find_package(Threads REQUIRED)

# Download and configure Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Set up include directories
set(CLER_SPSC_QUEUE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../include/cler_spsc-queue.hpp")

# Verify the header file exists
if(NOT EXISTS ${CLER_SPSC_QUEUE_HEADER})
    message(FATAL_ERROR "SPSC Queue header not found at: ${CLER_SPSC_QUEUE_HEADER}")
endif()

# Common compile options for all tests
set(SPSC_TEST_COMPILE_OPTIONS
    -std=c++17
    -Wall
    -Wextra
    -Werror
    -O2
    -g
    -pthread
    # Disable specific warnings that might come from the SPSC queue header
    -Wno-unused-variable
    -Wno-missing-braces
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-sign-compare
    -UNDEBUG  # Keep asserts enabled even in optimized builds
)

# Common link libraries for all tests
set(SPSC_TEST_LINK_LIBRARIES
    GTest::gtest_main
    Threads::Threads
)

# Function to create a test executable
function(add_spsc_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    
    target_compile_options(${TEST_NAME} PRIVATE ${SPSC_TEST_COMPILE_OPTIONS})
    
    target_include_directories(${TEST_NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    )
    
    target_link_libraries(${TEST_NAME} PRIVATE ${SPSC_TEST_LINK_LIBRARIES})
    
    # Register with CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # Set test properties
    set_target_properties(${TEST_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endfunction()

# Create test executables
add_spsc_test(spsc_basic_test test_spsc_basic.cpp)
add_spsc_test(spsc_batch_test test_spsc_batch.cpp)
add_spsc_test(spsc_bounds_test test_spsc_bounds.cpp)
add_spsc_test(spsc_doubly_mapped_test test_spsc_doubly_mapped.cpp)

# Optional: Create a combined test runner
add_executable(spsc_all_tests 
    test_spsc_basic.cpp
    test_spsc_batch.cpp
    test_spsc_bounds.cpp
    test_spsc_doubly_mapped.cpp
)

target_compile_options(spsc_all_tests PRIVATE ${SPSC_TEST_COMPILE_OPTIONS})
target_include_directories(spsc_all_tests PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(spsc_all_tests PRIVATE ${SPSC_TEST_LINK_LIBRARIES})

set_target_properties(spsc_all_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

add_test(NAME spsc_all_tests COMMAND spsc_all_tests)

# Print configuration info
message(STATUS "SPSC Queue Tests Configuration:")
message(STATUS "  Header file: ${CLER_SPSC_QUEUE_HEADER}")
message(STATUS "  CXX Standard: C++17")
message(STATUS "  Tests created: spsc_basic_test, spsc_batch_test, spsc_bounds_test, spsc_all_tests")

# Custom target for running all SPSC queue tests
add_custom_target(run_spsc_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure -R "spsc_"
    DEPENDS spsc_basic_test spsc_batch_test spsc_bounds_test spsc_doubly_mapped_test
    COMMENT "Running all SPSC Queue tests"
)

# Custom target for verbose test output
add_custom_target(run_spsc_tests_verbose
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure --verbose -R "spsc_"
    DEPENDS spsc_basic_test spsc_batch_test spsc_bounds_test spsc_doubly_mapped_test
    COMMENT "Running all SPSC Queue tests with verbose output"
)

# Performance test target (runs tests with different optimization levels)
add_custom_target(spsc_performance_test
    COMMAND echo "Running SPSC Queue performance tests..."
    COMMAND echo "Basic operations test:"
    COMMAND $<TARGET_FILE:spsc_basic_test> --gtest_filter="*NoSampleLoss*"
    COMMAND echo "Batch operations test:"  
    COMMAND $<TARGET_FILE:spsc_batch_test> --gtest_filter="*NoSampleLoss*"
    COMMAND echo "Bounds test:"
    COMMAND $<TARGET_FILE:spsc_bounds_test> --gtest_filter="*ConcurrentBoundsSafety*"
    DEPENDS spsc_basic_test spsc_batch_test spsc_bounds_test spsc_doubly_mapped_test
    COMMENT "Running SPSC Queue performance-focused tests"
)

# Memory safety test target (could be extended with valgrind/sanitizers)
add_custom_target(spsc_memory_test
    COMMAND echo "Running SPSC Queue memory safety tests..."
    COMMAND $<TARGET_FILE:spsc_bounds_test>
    DEPENDS spsc_bounds_test
    COMMENT "Running SPSC Queue memory safety tests"
)