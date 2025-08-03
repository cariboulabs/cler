# SPSC Queue Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Find required packages
find_package(Threads REQUIRED)

# GoogleTest is now provided by parent tests/CMakeLists.txt

# Enable testing
enable_testing()

# Set up include directories
set(CLER_SPSC_QUEUE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../include/cler_spsc-queue.hpp")

# Verify the header file exists
if(NOT EXISTS ${CLER_SPSC_QUEUE_HEADER})
    message(FATAL_ERROR "SPSC Queue header not found at: ${CLER_SPSC_QUEUE_HEADER}")
endif()

# Common link libraries for all tests
set(SPSC_TEST_LINK_LIBRARIES
    cler::cler
    GTest::gtest_main
    Threads::Threads
)

# Function to create a test executable
function(add_spsc_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    
    target_compile_options(${TEST_NAME} PRIVATE ${SPSC_TEST_COMPILE_OPTIONS})
    
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


# Print configuration info
message(STATUS "SPSC Queue Tests Configuration:")
message(STATUS "  Header file: ${CLER_SPSC_QUEUE_HEADER}")
message(STATUS "  CXX Standard: C++17")
message(STATUS "  Tests created: spsc_basic_test, spsc_batch_test, spsc_bounds_test, spsc_doubly_mapped_test")


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