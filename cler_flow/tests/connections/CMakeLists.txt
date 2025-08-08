# Connection routing tests
cmake_minimum_required(VERSION 3.14)

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
    # Create executable for each test
    add_executable(${test_name} ${test_name}.cpp)
    
    # Set C++ standard
    target_compile_features(${test_name} PRIVATE cxx_std_17)
    
    # Add compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test_name} PRIVATE -Wall -Wextra)
    endif()
    
    # Add the test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    
    # Set test properties - these tests should all pass
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 10
        LABELS "connection_routing"
    )
endforeach()

# Add a custom target to run all connection tests
add_custom_target(test_connections
    COMMAND ${CMAKE_CTEST_COMMAND} -L connection_routing --output-on-failure
    COMMENT "Running connection routing tests"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)