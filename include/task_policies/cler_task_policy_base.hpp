#pragma once

#include <cstdlib>

#include <type_traits>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace cler {

// C++17 compatible trait to validate TaskPolicy interface at compile time
template<typename T, typename = void>
struct is_valid_task_policy : std::false_type {};

template<typename T>
struct is_valid_task_policy<T, std::void_t<
    typename T::task_type,
    decltype(T::create_task(std::declval<void(*)()>())),
    decltype(T::join_task(std::declval<typename T::task_type&>())),
    decltype(T::yield()),
    decltype(T::sleep_us(std::declval<size_t>()))
>> : std::true_type {};

template<typename T>
constexpr bool is_valid_task_policy_v = is_valid_task_policy<T>::value;

struct BackoffState {
    size_t step = 0;
};

template<typename Derived>
struct TaskPolicyBase {
    static inline void relax() {
        Derived::yield();
        Derived::sleep_us(1);
    }

    static inline bool pin_to_core(size_t) { return false; }

    static inline void warn_unresolved_edge(const char*, const void*) {}

    [[noreturn]] static inline void fatal(const char*) { std::abort(); }

    static inline void report_partition_block(size_t, size_t, const char*, double) {}

    static inline void configure_thread_for_low_latency_sleep() {}

    static constexpr size_t park_fallback_sleep_us = 1000;

    static inline void park(const std::atomic<uint32_t>& sleep_epoch, uint32_t expected) {
        if (sleep_epoch.load(std::memory_order_acquire) != expected) return;
        Derived::sleep_us(park_fallback_sleep_us);
    }

    static inline void unpark(std::atomic<uint32_t>&) {}

    static inline void backoff(BackoffState& state) {
        Derived::relax();
        ++state.step;
    }

    static inline void backoff_reset(BackoffState& state) {
        state.step = 0;
    }

protected:
    TaskPolicyBase() = default;
    ~TaskPolicyBase() = default;
    
    // Helper to validate interface at compile time
    template<typename T = Derived>
    static constexpr bool validate_interface() {
        static_assert(is_valid_task_policy_v<T>, 
                     "TaskPolicy must implement the required interface");
        return true;
    }
};

} // namespace cler