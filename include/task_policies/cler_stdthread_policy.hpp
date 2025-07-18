#pragma once

#include "cler.hpp"
#include <thread>

namespace cler {

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