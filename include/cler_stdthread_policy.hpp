#pragma once

#include "cler.hpp"
#include <thread>

namespace cler {

/**
 * Standard Thread Policy for CLER FlowGraph
 * 
 * Usage:
 * 1. Include this file for desktop/server applications
 * 2. Use with FlowGraph template parameter:
 *    FlowGraph<StdThreadPolicy, BlockRunner<...>, ...> flowgraph(...);
 * 3. Call flowgraph.run() from your main application
 * 
 * Requirements:
 * - C++11 or later with std::thread support
 * - Threading library linked (-lpthread on Linux)
 * 
 * Features:
 * - Uses std::thread for cross-platform threading
 * - Supports adaptive sleep for power efficiency
 * - Clean shutdown with proper thread joining
 */

struct StdThreadPolicy {
    using thread_type = std::thread;
    
    template<typename Func>
    static thread_type create_thread(Func&& f) {
        return std::thread(std::forward<Func>(f));
    }
    
    static void join_thread(thread_type& t) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    static void yield() {
        std::this_thread::yield();
    }
    
    static void sleep_us(size_t microseconds) {
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    }
};

// Convenient alias for std::thread-based FlowGraph
template<typename... BlockRunners>
using DesktopFlowGraph = FlowGraph<StdThreadPolicy, BlockRunners...>;

} // namespace cler

/*
Example usage:

#include "cler.hpp"
#include "cler_stdthread_policy.hpp"

int main() {
    // Your blocks
    MySourceBlock source("Source");
    MyProcessBlock processor("Processor");
    MySinkBlock sink("Sink");
    
    // Channels
    cler::Channel<float, 1024> ch1;
    cler::Channel<float, 1024> ch2;
    
    // Create desktop FlowGraph
    cler::DesktopFlowGraph flowgraph(
        cler::BlockRunner(&source, &ch1),
        cler::BlockRunner(&processor, &ch1, &ch2),
        cler::BlockRunner(&sink, &ch2)
    );
    
    flowgraph.run();
    
    // Let it run for some time
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    flowgraph.stop();
    
    return 0;
}
*/