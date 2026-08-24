# Tests for app-level code under desktop_examples/. Kept out of the desktop_blocks
# suite so the library tests only ever include the library.
if (TARGET cler::blocks_web AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(websdr_app_test test_websdr_app.cpp)
    target_link_libraries(websdr_app_test gtest_main cler::desktop_blocks cler::blocks_web)
    add_test(NAME websdr_app_test COMMAND websdr_app_test)
endif()
