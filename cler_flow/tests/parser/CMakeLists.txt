cmake_minimum_required(VERSION 3.14)
project(cler_flow_parser_tests)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find libclang (use parent's detection)
if(LIBCLANG_FOUND)
    # Test executable for block parser
    add_executable(test_block_parser 
        test_block_parser.cpp
        ${CMAKE_SOURCE_DIR}/src/block_parser.cpp
    )
    
    target_include_directories(test_block_parser PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBCLANG_INCLUDE_DIR}
    )
    
    target_link_libraries(test_block_parser PRIVATE
        ${LIBCLANG_LIBRARY}
    )
    
    target_compile_definitions(test_block_parser PRIVATE HAS_LIBCLANG)
    
    # Test executable for library scanner
    add_executable(test_scanner
        test_scanner.cpp
        ${CMAKE_SOURCE_DIR}/src/block_parser.cpp
    )
    
    target_include_directories(test_scanner PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBCLANG_INCLUDE_DIR}
    )
    
    target_link_libraries(test_scanner PRIVATE
        ${LIBCLANG_LIBRARY}
    )
    
    target_compile_definitions(test_scanner PRIVATE HAS_LIBCLANG)
    
    # Add tests
    enable_testing()
    add_test(NAME parser_quick_check COMMAND test_block_parser /home/alon/repos/cler/desktop_blocks/math/add.hpp)
    add_test(NAME parser_all_blocks COMMAND test_block_parser)
    
    # Custom target to run tests
    add_custom_target(run_parser_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS test_block_parser test_scanner
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
else()
    message(WARNING "libclang not found - parser tests will not be built")
endif()