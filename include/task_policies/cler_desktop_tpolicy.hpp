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