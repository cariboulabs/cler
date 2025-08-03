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

if (CLER_BUILD_BLOCKS_LIQUID)
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
endif()

# Register tests with CTest for discovery
add_test(NAME math_blocks_test COMMAND math_blocks_test)
add_test(NAME utility_blocks_test COMMAND utility_blocks_test)
add_test(NAME sink_blocks_test COMMAND sink_blocks_test)
add_test(NAME noise_blocks_test COMMAND noise_blocks_test)

if (CLER_BUILD_BLOCKS_LIQUID)
    add_test(NAME channelizer_blocks_test COMMAND channelizer_blocks_test)
    add_test(NAME resampler_blocks_test COMMAND resampler_blocks_test)
else()
    message(STATUS "Skipping channelizer_blocks_test because CLER_BUILD_BLOCKS_LIQUID is OFF")
    message(STATUS "Skipping resampler_blocks_test because CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()