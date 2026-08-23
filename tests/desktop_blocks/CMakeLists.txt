# Math blocks tests
add_executable(math_blocks_test
    test_math_blocks.cpp
)

target_link_libraries(math_blocks_test
    gtest_main
    cler::desktop_blocks
)

# Utility blocks tests
add_executable(utility_blocks_test
    test_utility_blocks.cpp
)

target_link_libraries(utility_blocks_test
    gtest_main
    cler::desktop_blocks
)

# Sink blocks tests
add_executable(sink_blocks_test
    test_sink_blocks.cpp
)

target_link_libraries(sink_blocks_test
    gtest_main
    cler::desktop_blocks
)

# SigMF blocks tests
add_executable(sigmf_blocks_test
    test_sigmf_blocks.cpp
)

target_link_libraries(sigmf_blocks_test
    gtest_main
    cler::desktop_blocks
)

# Noise blocks tests
add_executable(noise_blocks_test
    test_noise_blocks.cpp
)

target_link_libraries(noise_blocks_test
    gtest_main
    cler::desktop_blocks
)

if (CLER_BUILD_BLOCKS_LIQUID)
    # Resampler blocks tests
    add_executable(resampler_blocks_test
        test_resampler_blocks.cpp
    )

    target_link_libraries(resampler_blocks_test
        gtest_main
        cler::desktop_blocks
    )

    add_executable(fm_blocks_test test_fm_blocks.cpp)
    target_link_libraries(fm_blocks_test gtest_main cler::desktop_blocks)
    add_executable(demod_blocks_test test_demod_blocks.cpp)
    target_link_libraries(demod_blocks_test gtest_main cler::desktop_blocks)
    add_executable(ais_blocks_test test_ais_blocks.cpp)
    target_link_libraries(ais_blocks_test gtest_main cler::desktop_blocks)
    add_executable(aprs_blocks_test test_aprs_blocks.cpp)
    target_link_libraries(aprs_blocks_test gtest_main cler::desktop_blocks)

    add_executable(linear_modem_blocks_test test_linear_modem_blocks.cpp)
    target_link_libraries(linear_modem_blocks_test gtest_main cler::desktop_blocks)

    add_executable(fec_blocks_test test_fec_blocks.cpp)
    target_link_libraries(fec_blocks_test gtest_main cler::desktop_blocks)
    add_executable(spectrum_block_test test_spectrum_block.cpp)
    target_link_libraries(spectrum_block_test gtest_main cler::desktop_blocks)

    # Channelizer blocks tests
    add_executable(channelizer_blocks_test
        test_channelizer_blocks.cpp
    )

    target_link_libraries(channelizer_blocks_test
        gtest_main
        cler::desktop_blocks
    )
endif()

add_executable(source_mux_test test_source_mux.cpp)
target_link_libraries(source_mux_test gtest_main cler::desktop_blocks)
add_test(NAME source_mux_test COMMAND source_mux_test)

add_executable(gui_render_test
    test_gui_render.cpp
)

target_link_libraries(gui_render_test
    gtest_main
    cler::desktop_blocks
)

# Register tests with CTest for discovery
add_test(NAME gui_render_test COMMAND gui_render_test)
add_test(NAME math_blocks_test COMMAND math_blocks_test)
add_test(NAME utility_blocks_test COMMAND utility_blocks_test)
add_test(NAME sink_blocks_test COMMAND sink_blocks_test)
add_test(NAME noise_blocks_test COMMAND noise_blocks_test)
add_test(NAME sigmf_blocks_test COMMAND sigmf_blocks_test)

if (CLER_BUILD_BLOCKS_LIQUID)
    add_test(NAME channelizer_blocks_test COMMAND channelizer_blocks_test)
    add_test(NAME resampler_blocks_test COMMAND resampler_blocks_test)
    add_test(NAME fm_blocks_test COMMAND fm_blocks_test)
    add_test(NAME linear_modem_blocks_test COMMAND linear_modem_blocks_test)
    add_test(NAME fec_blocks_test COMMAND fec_blocks_test)
    add_test(NAME ais_blocks_test COMMAND ais_blocks_test)
    add_test(NAME demod_blocks_test COMMAND demod_blocks_test)
    add_test(NAME aprs_blocks_test COMMAND aprs_blocks_test)
    add_test(NAME spectrum_block_test COMMAND spectrum_block_test)
else()
    message(STATUS "Skipping channelizer_blocks_test because CLER_BUILD_BLOCKS_LIQUID is OFF")
    message(STATUS "Skipping resampler_blocks_test because CLER_BUILD_BLOCKS_LIQUID is OFF")
endif()
if(HACKRF_FOUND)
    add_executable(hackrf_blocks_test
        test_hackrf_blocks.cpp
    )

    target_link_libraries(hackrf_blocks_test
        gtest_main
        cler::desktop_blocks
    )

    add_test(NAME hackrf_blocks_test COMMAND hackrf_blocks_test)
else()
    message(STATUS "Skipping hackrf_blocks_test because HACKRF_FOUND is OFF")
endif()

if (CLER_BUILD_BLOCKS_WEB)
    add_executable(web_blocks_test test_web_blocks.cpp)
    target_link_libraries(web_blocks_test gtest_main cler::desktop_blocks)
    add_test(NAME web_blocks_test COMMAND web_blocks_test)
endif()

find_program(NODE_EXECUTABLE node)
if (NODE_EXECUTABLE)
    add_test(NAME websdr_client_test COMMAND ${NODE_EXECUTABLE} --test
        ${PROJECT_SOURCE_DIR}/desktop_examples/websdr/client_tests/proto.test.mjs
        ${PROJECT_SOURCE_DIR}/desktop_examples/websdr/client_tests/panel.test.mjs)
endif()
