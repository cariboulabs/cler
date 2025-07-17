#include <gtest/gtest.h>
#include "cler_stdthread_policy.hpp"
#include <atomic>
#include <chrono>

class ThreadingSimpleTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test basic thread creation and joining
TEST_F(ThreadingSimpleTest, BasicThreadOperations) {
    std::atomic<bool> executed{false};
    
    auto thread = cler::StdThreadPolicy::create_thread([&]() {
        executed = true;
    });
    
    EXPECT_TRUE(thread.joinable());
    
    cler::StdThreadPolicy::join(thread);
    EXPECT_TRUE(executed.load());
    EXPECT_FALSE(thread.joinable());
}

// Test thread creation with parameters
TEST_F(ThreadingSimpleTest, ThreadWithParameters) {
    std::atomic<int> result{0};
    
    auto thread = cler::StdThreadPolicy::create_thread([&](int value) {
        result = value * 2;
    }, 21);
    
    cler::StdThreadPolicy::join(thread);
    EXPECT_EQ(result.load(), 42);
}

// Test thread detach
TEST_F(ThreadingSimpleTest, ThreadDetach) {
    std::atomic<bool> executed{false};
    
    auto thread = cler::StdThreadPolicy::create_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        executed = true;
    });
    
    EXPECT_TRUE(thread.joinable());
    cler::StdThreadPolicy::detach(thread);
    EXPECT_FALSE(thread.joinable());
    
    // Give detached thread time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(executed.load());
}

// Test simple block runner
TEST_F(ThreadingSimpleTest, SimpleBlockRunner) {
    std::atomic<int> counter{0};
    
    auto simple_func = [&counter]() -> cler::Result<cler::Empty, cler::Error> {
        counter++;
        return cler::Empty{};
    };
    
    cler::BlockRunner runner(simple_func);
    
    auto result = runner.run();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(counter.load(), 1);
    
    // Test multiple runs
    for (int i = 0; i < 5; ++i) {
        auto result = runner.run();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(counter.load(), 6);
}