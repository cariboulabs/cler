#pragma once

#include "cler.hpp"
#include "cler_task_policy_base.hpp"
#include <thread>
#include <chrono>
#include <algorithm>
#ifdef __linux__
#include <sys/prctl.h>
#endif

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
    
    static inline void relax() {
        platform::spin_wait(64);
    }

    static constexpr size_t backoff_spin_steps = 8;
    static constexpr size_t backoff_yield_steps = 16;
    static constexpr size_t backoff_initial_sleep_us = 1;
    static constexpr size_t backoff_max_sleep_us = 1000;
    static constexpr size_t backoff_max_shift = 10;

    static inline void backoff(BackoffState& state) {
        if (state.step < backoff_spin_steps) {
            platform::spin_wait(64);
        } else if (state.step < backoff_yield_steps) {
            std::this_thread::yield();
        } else {
            size_t shift = (std::min)(state.step - backoff_yield_steps, backoff_max_shift);
            size_t sleep_us_value = (std::min)(backoff_initial_sleep_us << shift, backoff_max_sleep_us);
            sleep_us(sleep_us_value);
        }
        ++state.step;
    }

    static inline void backoff_reset(BackoffState& state) {
        state.step = 0;
    }

    static inline void pin_to_core(size_t worker_id) {
        platform::set_thread_affinity(worker_id);
    }

    static inline void configure_thread_for_low_latency_sleep() {
#ifdef __linux__
        prctl(PR_SET_TIMERSLACK, 1);
#endif
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