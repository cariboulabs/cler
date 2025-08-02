# Desktop Blocks Tests
cmake_minimum_required(VERSION 3.20)

# Math blocks tests
add_executable(math_blocks_test
    test_math_blocks.cpp
)

target_link_libraries(math_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

# Utility blocks tests
add_executable(utility_blocks_test
    test_utility_blocks.cpp
)

target_link_libraries(utility_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

# Sink blocks tests
add_executable(sink_blocks_test
    test_sink_blocks.cpp
)

target_link_libraries(sink_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

# Noise blocks tests
add_executable(noise_blocks_test
    test_noise_blocks.cpp
)

target_link_libraries(noise_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

# Resampler blocks tests
add_executable(resampler_blocks_test
    test_resampler_blocks.cpp
)

target_link_libraries(resampler_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

# Channelizer blocks tests
add_executable(channelizer_blocks_test
    test_channelizer_blocks.cpp
)

target_link_libraries(channelizer_blocks_test
    gtest_main
    cler::cler_desktop_blocks
)

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
    cler::cler_desktop_blocks
)

add_dependencies(desktop_blocks_all_tests gtest)

