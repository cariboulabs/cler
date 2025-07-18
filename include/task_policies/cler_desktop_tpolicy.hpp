#pragma once

#include "cler_task_policy_base.hpp"
#include <thread>
#include <chrono>

namespace cler {

struct StdTaskPolicy {
    using task_type = std::thread;
    
    template<typename Func>
    static task_type create_task(Func&& f) {
        return std::thread(std::forward<Func>(f));
    }
    
    static void join_task(task_type& t) {
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

// Forward declaration for convenient alias
template<typename TaskPolicy, typename... BlockRunners>
class FlowGraph;

// Convenient helper function for C++17 compatibility
template<typename... BlockRunners>
auto make_desktop_flowgraph(BlockRunners&&... runners) {
    return FlowGraph<StdTaskPolicy, BlockRunners...>(std::forward<BlockRunners>(runners)...);
}

// Convenient alias for std::thread-based FlowGraph (C++20 only)
template<typename... BlockRunners>
using DesktopFlowGraph = FlowGraph<StdTaskPolicy, BlockRunners...>;

} // namespace cler