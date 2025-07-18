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