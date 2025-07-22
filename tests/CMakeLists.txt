# Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

message(STATUS "Building CLER Tests")

# Enable testing for this directory and subdirectories
enable_testing()

# Add test subdirectories
add_subdirectory(spsc-queue)

# Custom target to run all tests
add_custom_target(run_all_cler_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure
    COMMENT "Running all CLER tests"
)

# Custom target for verbose test output
add_custom_target(run_all_cler_tests_verbose
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure --verbose
    COMMENT "Running all CLER tests with verbose output"
)

message(STATUS "CLER Tests configured. Use 'make run_all_cler_tests' to run all tests.")