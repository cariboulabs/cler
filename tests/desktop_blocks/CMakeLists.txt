# Desktop Blocks Tests
cmake_minimum_required(VERSION 3.20)

# Math blocks tests
add_executable(math_blocks_test
    test_math_blocks.cpp
)

target_link_libraries(math_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(math_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Utility blocks tests
add_executable(utility_blocks_test
    test_utility_blocks.cpp
)

target_link_libraries(utility_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(utility_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Sink blocks tests
add_executable(sink_blocks_test
    test_sink_blocks.cpp
)

# Simple sink blocks tests (for debugging)
add_executable(sink_blocks_simple_test
    test_sink_blocks_simple.cpp
)

target_link_libraries(sink_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_link_libraries(sink_blocks_simple_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(sink_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(sink_blocks_simple_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Noise blocks tests
add_executable(noise_blocks_test
    test_noise_blocks.cpp
)

target_link_libraries(noise_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(noise_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Resampler blocks tests
add_executable(resampler_blocks_test
    test_resampler_blocks.cpp
)

target_link_libraries(resampler_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(resampler_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Channelizer blocks tests
add_executable(channelizer_blocks_test
    test_channelizer_blocks.cpp
)

target_link_libraries(channelizer_blocks_test
    gtest_main
    cler_logger
    liquid
)

target_include_directories(channelizer_blocks_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

# Add test targets to the build
add_dependencies(math_blocks_test gtest)
add_dependencies(utility_blocks_test gtest)
add_dependencies(sink_blocks_test gtest)
add_dependencies(sink_blocks_simple_test gtest)
add_dependencies(noise_blocks_test gtest)
add_dependencies(resampler_blocks_test gtest)
add_dependencies(channelizer_blocks_test gtest)

# Create a combined test executable like spsc_all_tests
add_executable(desktop_blocks_all_tests
    test_math_blocks.cpp
    test_utility_blocks.cpp
    test_sink_blocks.cpp
    test_noise_blocks.cpp
    test_resampler_blocks.cpp
    test_channelizer_blocks.cpp
)

target_link_libraries(desktop_blocks_all_tests
    gtest_main
    cler_logger
    liquid
)

target_include_directories(desktop_blocks_all_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/desktop_blocks
    ${CMAKE_SOURCE_DIR}/include
)

add_dependencies(desktop_blocks_all_tests gtest)

