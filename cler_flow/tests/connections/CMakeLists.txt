# Standalone CMakeLists.txt for connection routing tests
cmake_minimum_required(VERSION 3.14)
project(connection_routing_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# List of test executables
set(CONNECTION_TESTS
    test_fillets
    test_all_elbows
    test_horizontal_routing
    test_forward_polyline
    test_elbows
    test_elbows_with_nodes
    test_vertical_fillet
)

# Create test executables and add to CTest
foreach(test_name ${CONNECTION_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()