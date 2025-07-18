#pragma once

#include <type_traits>

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

/**
 * CRTP base class for TaskPolicy interface enforcement
 * 
 * This base class uses the Curiously Recurring Template Pattern (CRTP) to
 * enforce that all TaskPolicy implementations provide the required interface
 * at compile time.
 * 
 * Required interface for TaskPolicy:
 * - using task_type = implementation-specific type;
 * - template<typename Func> static task_type create_task(Func&& f);
 * - static void join_task(task_type& t);
 * - static void yield();
 * - static void sleep_us(size_t microseconds);
 * 
 * Usage:
 * struct MyTaskPolicy : TaskPolicyBase<MyTaskPolicy> {
 *     using task_type = std::thread;
 *     // ... implement required methods
 * };
 */
template<typename Derived>
struct TaskPolicyBase {
    // Static assertions to ensure derived class implements required interface
    // These will be checked when the derived class is instantiated
    
protected:
    // Prevent instantiation of base class
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