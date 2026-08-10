#pragma once

#include <cstdlib>

#include "cler.hpp"
#include "cler_task_policy_base.hpp"
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <ctime>
#include <climits>
#else
#include <mutex>
#include <condition_variable>
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

    static inline bool pin_to_core(size_t worker_id) {
        return platform::set_thread_affinity(worker_id);
    }

    static inline void warn_unresolved_edge(const char* producer_name, const void* address) {
        std::fprintf(stderr,
            "cler: unresolved output edge from block '%s' at %p (no consumer found; "
            "edge derivation matches channels stored inside the consuming block's object). "
            "PinnedIslands will fall back to a contiguous partition.\n",
            producer_name, address);
    }

    [[noreturn]] static inline void fatal(const char* msg, const char* detail) {
        std::fprintf(stderr, "cler: fatal: %s%s%s\n", msg,
                     (detail && *detail) ? ": " : "", (detail && *detail) ? detail : "");
        std::abort();
    }

    static inline void report_partition_block(size_t island, size_t position, const char* name,
                                              double weight_ns_per_item) {
        std::fprintf(stderr, "cler: partition island %zu pos %zu %s (%.1f ns/item)\n",
                     island, position, name, weight_ns_per_item);
    }

    static constexpr size_t park_timeout_us = 1000;

    static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t),
                  "park/unpark require a lock-free uint32_t-sized atomic");

#ifdef __linux__
    static inline uint32_t* futex_word(const std::atomic<uint32_t>& sleep_epoch) {
        return const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&sleep_epoch));
    }

    static inline void park(const std::atomic<uint32_t>& sleep_epoch, uint32_t expected) {
        struct timespec timeout{0, static_cast<long>(park_timeout_us * 1000)};
        syscall(SYS_futex, futex_word(sleep_epoch), FUTEX_WAIT_PRIVATE, expected, &timeout, nullptr, 0);
    }

    static inline void unpark(std::atomic<uint32_t>& sleep_epoch) {
        syscall(SYS_futex, futex_word(sleep_epoch), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
    }
#else
    struct ParkSlot {
        std::mutex mutex;
        std::condition_variable cv;
    };

    static inline ParkSlot& park_slot(const std::atomic<uint32_t>& sleep_epoch) {
        static ParkSlot slots[16];
        return slots[(reinterpret_cast<uintptr_t>(&sleep_epoch) / 64) % 16];
    }

    static inline void park(const std::atomic<uint32_t>& sleep_epoch, uint32_t expected) {
        ParkSlot& slot = park_slot(sleep_epoch);
        std::unique_lock<std::mutex> lock(slot.mutex);
        if (sleep_epoch.load(std::memory_order_acquire) != expected) return;
        slot.cv.wait_for(lock, std::chrono::microseconds(park_timeout_us));
    }

    static inline void unpark(std::atomic<uint32_t>& sleep_epoch) {
        ParkSlot& slot = park_slot(sleep_epoch);
        std::lock_guard<std::mutex> lock(slot.mutex);
        slot.cv.notify_all();
    }
#endif

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