find_package(Threads REQUIRED)

add_executable(scheduler_cost_test
    test_scheduler_cost.cpp
)

target_link_libraries(scheduler_cost_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME scheduler_cost_test COMMAND scheduler_cost_test)

add_executable(edge_derivation_test
    test_edge_derivation.cpp
)

target_link_libraries(edge_derivation_test
    gtest_main
    cler::desktop_blocks
    Threads::Threads
)

add_test(NAME edge_derivation_test COMMAND edge_derivation_test)

add_executable(may_block_test
    test_may_block.cpp
)

target_link_libraries(may_block_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME may_block_test COMMAND may_block_test)

add_executable(pinned_islands_test
    test_pinned_islands.cpp
)

target_link_libraries(pinned_islands_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME pinned_islands_test COMMAND pinned_islands_test)
