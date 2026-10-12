# Tests for app-level code under desktop_examples/. Kept out of the desktop_blocks
# suite so the library tests only ever include the library.
if (TARGET cler::blocks_web AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(earshot_app_test test_earshot_app.cpp)
    target_link_libraries(earshot_app_test gtest_main cler::desktop_blocks cler::blocks_web)
    add_test(NAME earshot_app_test COMMAND earshot_app_test)
endif()

# bpsk_link: a --sim recording replayed with --replay must decode the same messages.
if (TARGET bpsk_link)
    add_test(NAME bpsk_link_replay_test
        COMMAND ${CMAKE_COMMAND} -DBPSK_LINK=$<TARGET_FILE:bpsk_link> -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/bpsk_link_replay_test.cmake)
    set_tests_properties(bpsk_link_replay_test PROPERTIES TIMEOUT 120)
endif()
