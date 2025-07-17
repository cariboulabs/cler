#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_stdthread_policy.hpp"

// Only test standard threading policy since RTOS policies require specific environments
#include <atomic>
#include <chrono>

class ThreadingPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test StdThreadPolicy basic functionality
TEST_F(ThreadingPolicyTest, StdThreadPolicyBasics) {
    std::atomic<bool> function_executed{false};
    
    // Test thread creation
    auto thread = cler::StdThreadPolicy::create_thread([&]() {
        function_executed = true;
    });
    
    ASSERT_TRUE(thread.joinable());
    
    // Test join functionality
    cler::StdThreadPolicy::join_thread(thread);
    EXPECT_TRUE(function_executed.load());
    EXPECT_FALSE(thread.joinable());
}

// Test StdThreadPolicy with parameters
TEST_F(ThreadingPolicyTest, StdThreadPolicyWithParameters) {
    std::atomic<int> result{0};
    
    // Note: create_thread only accepts Func, not Args, so we need to capture the value
    auto thread = cler::StdThreadPolicy::create_thread([&]() {
        result = 21 * 2;
    });
    
    cler::StdThreadPolicy::join_thread(thread);
    EXPECT_EQ(result.load(), 42);
}

// Test StdThreadPolicy basic functionality 
TEST_F(ThreadingPolicyTest, StdThreadPolicyYield) {
    // Test that yield doesn't crash (can't really test much more)
    cler::StdThreadPolicy::yield();
    
    // Test sleep_us doesn't crash
    cler::StdThreadPolicy::sleep_us(1);
    
    SUCCEED(); // If we get here, yield and sleep_us work
}

// Test simple block with std::thread policy
TEST_F(ThreadingPolicyTest, SimpleBlockWithStdThread) {
    std::atomic<int> execution_count{0};
    
    // Simple test block
    struct TestBlock : cler::BlockBase {
        std::atomic<int>& counter;
        
        TestBlock(std::atomic<int>& c) : cler::BlockBase("TestBlock"), counter(c) {}
        
        cler::Result<cler::Empty, cler::Error> procedure() {
            counter++;
            return cler::Empty{};
        }
    };
    
    TestBlock block(execution_count);
    
    // Test direct block execution
    auto result = block.procedure();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(execution_count.load(), 1);
    
    // Test multiple executions
    for (int i = 0; i < 5; ++i) {
        auto result = block.procedure();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(execution_count.load(), 6);
}

// Test flow graph with std::thread policy
TEST_F(ThreadingPolicyTest, FlowGraphWithStdThread) {
    std::atomic<int> block1_count{0};
    std::atomic<int> block2_count{0};
    
    // Create test blocks
    struct TestBlock1 : cler::BlockBase {
        std::atomic<int>& counter;
        TestBlock1(std::atomic<int>& c) : cler::BlockBase("TestBlock1"), counter(c) {}
        cler::Result<cler::Empty, cler::Error> procedure() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return cler::Empty{};
        }
    };
    
    struct TestBlock2 : cler::BlockBase {
        std::atomic<int>& counter;
        TestBlock2(std::atomic<int>& c) : cler::BlockBase("TestBlock2"), counter(c) {}
        cler::Result<cler::Empty, cler::Error> procedure() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return cler::Empty{};
        }
    };
    
    TestBlock1 block1(block1_count);
    TestBlock2 block2(block2_count);
    
    // Create block runners (no output channels for this simple test)
    cler::BlockRunner runner1(&block1);
    cler::BlockRunner runner2(&block2);
    
    // Create flow graph with std::thread policy
    cler::FlowGraph<cler::StdThreadPolicy, decltype(runner1), decltype(runner2)> 
        flowgraph(runner1, runner2);
    
    // Start the flow graph
    flowgraph.run();
    
    // Let it run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Stop the flow graph
    flowgraph.stop();
    
    // Verify both blocks executed
    EXPECT_GT(block1_count.load(), 0);
    EXPECT_GT(block2_count.load(), 0);
}

// Test flow graph statistics with std::thread policy
TEST_F(ThreadingPolicyTest, FlowGraphStatistics) {
    std::atomic<int> execution_count{0};
    
    struct CountingBlock : cler::BlockBase {
        std::atomic<int>& counter;
        CountingBlock(std::atomic<int>& c) : cler::BlockBase("CountingBlock"), counter(c) {}
        cler::Result<cler::Empty, cler::Error> procedure() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return cler::Empty{};
        }
    };
    
    CountingBlock block(execution_count);
    cler::BlockRunner runner(&block);
    cler::FlowGraph<cler::StdThreadPolicy, decltype(runner)> flowgraph(runner);
    
    // Start and run for a bit
    flowgraph.run();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    flowgraph.stop();
    
    // Get statistics
    auto stats = flowgraph.stats();
    EXPECT_EQ(stats.size(), 1);  // One block
    
    const auto& block_stats = stats[0];
    EXPECT_GT(block_stats.successful_procedures, 0);
    
    // Verify execution count matches our atomic counter
    EXPECT_EQ(block_stats.successful_procedures, execution_count.load());
}

// Test error handling in flow graph
TEST_F(ThreadingPolicyTest, FlowGraphErrorHandling) {
    std::atomic<int> successful_runs{0};
    std::atomic<int> failed_runs{0};
    
    struct ErrorProneBlock : cler::BlockBase {
        std::atomic<int>& success_counter;
        std::atomic<int>& failure_counter;
        int call_count = 0;
        
        ErrorProneBlock(std::atomic<int>& s, std::atomic<int>& f) 
            : cler::BlockBase("ErrorProneBlock"), success_counter(s), failure_counter(f) {}
        
        cler::Result<cler::Empty, cler::Error> procedure() {
            call_count++;
            
            if (call_count % 3 == 0) {
                failure_counter++;
                return cler::Error::ProcedureError;
            } else {
                success_counter++;
                return cler::Empty{};
            }
        }
    };
    
    ErrorProneBlock block(successful_runs, failed_runs);
    cler::BlockRunner runner(&block);
    cler::FlowGraph<cler::StdThreadPolicy, decltype(runner)> flowgraph(runner);
    
    // Start and run for a bit
    flowgraph.run();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    flowgraph.stop();
    
    // Verify we had both successful and failed runs
    EXPECT_GT(successful_runs.load(), 0);
    EXPECT_GT(failed_runs.load(), 0);
    
    // Get statistics and verify error counting
    auto stats = flowgraph.stats();
    EXPECT_EQ(stats.size(), 1);
    
    const auto& block_stats = stats[0];
    EXPECT_EQ(block_stats.successful_procedures + block_stats.failed_procedures, 
              successful_runs.load() + failed_runs.load());
    EXPECT_GT(block_stats.failed_procedures, 0);
}

// Test thread policy type traits
TEST_F(ThreadingPolicyTest, ThreadPolicyTraits) {
    // Verify StdThreadPolicy has the expected thread type
    static_assert(std::is_same<cler::StdThreadPolicy::thread_type, std::thread>::value);
    
    // Test that we can create threads with the policy
    bool test_passed = false;
    auto thread = cler::StdThreadPolicy::create_thread([&test_passed]() {
        test_passed = true;
    });
    
    cler::StdThreadPolicy::join_thread(thread);
    EXPECT_TRUE(test_passed);
}

// Test concurrent flow graphs
TEST_F(ThreadingPolicyTest, ConcurrentFlowGraphs) {
    constexpr int NUM_GRAPHS = 3;
    std::array<std::atomic<int>, NUM_GRAPHS> counters{};
    
    // Create blocks for each graph
    struct TestBlock : cler::BlockBase {
        std::atomic<int>& counter;
        TestBlock(std::atomic<int>& c, int id) 
            : cler::BlockBase("TestBlock" + std::to_string(id)), counter(c) {}
        cler::Result<cler::Empty, cler::Error> procedure() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return cler::Empty{};
        }
    };
    
    // Create multiple blocks and flowgraphs
    std::array<std::unique_ptr<TestBlock>, NUM_GRAPHS> blocks;
    std::array<std::unique_ptr<cler::BlockRunner<TestBlock>>, NUM_GRAPHS> runners;
    std::array<std::unique_ptr<cler::FlowGraph<cler::StdThreadPolicy, 
        cler::BlockRunner<TestBlock>>>, NUM_GRAPHS> flowgraphs;
    
    for (int i = 0; i < NUM_GRAPHS; ++i) {
        blocks[i] = std::make_unique<TestBlock>(counters[i], i);
        runners[i] = std::make_unique<cler::BlockRunner<TestBlock>>(blocks[i].get());
        flowgraphs[i] = std::make_unique<cler::FlowGraph<cler::StdThreadPolicy, 
            cler::BlockRunner<TestBlock>>>(*runners[i]);
    }
    
    // Start all flow graphs
    for (auto& graph : flowgraphs) {
        graph->run();
    }
    
    // Let them run
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Stop all flow graphs
    for (auto& graph : flowgraphs) {
        graph->stop();
    }
    
    // Verify all graphs executed
    for (int i = 0; i < NUM_GRAPHS; ++i) {
        EXPECT_GT(counters[i].load(), 0) << "Graph " << i << " did not execute";
    }
}