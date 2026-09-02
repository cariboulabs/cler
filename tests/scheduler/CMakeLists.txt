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
# needs 3 threads of its own; sharing a 4-vCPU CI runner with 3 other ctest jobs
# starved a whole worker partition and made the throughput asserts flaky
set_tests_properties(may_block_test PROPERTIES RUN_SERIAL TRUE)

add_executable(pinned_islands_test
    test_pinned_islands.cpp
)

target_link_libraries(pinned_islands_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME pinned_islands_test COMMAND pinned_islands_test)

add_executable(fused_block_test
    test_fused_block.cpp
)

target_link_libraries(fused_block_test
    gtest_main
    cler::desktop_blocks
    Threads::Threads
)

add_test(NAME fused_block_test COMMAND fused_block_test)

add_executable(idle_progress_contract_test
    test_idle_progress_contract.cpp
)

target_link_libraries(idle_progress_contract_test
    gtest_main
    cler::desktop_blocks
    Threads::Threads
)

add_test(NAME idle_progress_contract_test COMMAND idle_progress_contract_test)

add_executable(lifecycle_test
    test_lifecycle.cpp
)

target_link_libraries(lifecycle_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME lifecycle_test COMMAND lifecycle_test)

add_executable(repartition_stress_test
    test_repartition_stress.cpp
)

target_link_libraries(repartition_stress_test
    gtest_main
    cler::cler
    Threads::Threads
)

add_test(NAME repartition_stress_test COMMAND repartition_stress_test)
