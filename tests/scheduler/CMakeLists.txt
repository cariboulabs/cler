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
