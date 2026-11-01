# Tests for app-level code under desktop_examples/. Kept out of the desktop_blocks
# suite so the library tests only ever include the library.
if (TARGET cler::blocks_web AND CLER_BUILD_BLOCKS_LIQUID)
    add_executable(earshot_app_test test_earshot_app.cpp)
    target_link_libraries(earshot_app_test gtest_main cler::desktop_blocks cler::blocks_web)
    add_test(NAME earshot_app_test COMMAND earshot_app_test)
endif()
