#pragma once

#include "cler.hpp"
#include "cler_task_policy_base.hpp"
#include <thread>
#include <chrono>

namespace cler {

struct DesktopTaskPolicy : TaskPolicyBase<DesktopTaskPolicy> {
    using task_type = std::thread;

    template <typename F>
    static task_type create_task(F&& func) {
        return std::thread(std::forward<F>(func));
    }

    static void join_task(task_type& t) {
        if (t.joinable()) t.join();
    }

    static void yield() {
        std::this_thread::yield();
    }

    static void sleep_us(size_t us) {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
    
    // Efficient pause that reduces CPU contention
    // Uses platform-specific spin hints, then backs off with a tiny sleep
    static inline void relax() {
        platform::spin_wait(64);  // Spin briefly with CPU-specific hints
        sleep_us(1);              // Then back off to reduce contention
    }
    
    // Pin worker thread to specific CPU core for better cache locality
    static inline void pin_to_core(size_t worker_id) {
        platform::set_thread_affinity(worker_id);
    }
};


template<typename... Runners>
auto make_desktop_flowgraph(Runners&&... runners) {
    return cler::FlowGraph<cler::DesktopTaskPolicy, std::decay_t<Runners>...>(
        std::forward<Runners>(runners)...
    );
}

template<typename... BlockRunners>
using DesktopFlowGraph = FlowGraph<DesktopTaskPolicy, BlockRunners...>;

} // namespace cler