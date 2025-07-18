#pragma once

#include <cstddef>

namespace cler {

/**
 * Documentation for task policy interface
 * 
 * All task policies must implement the following interface:
 * 
 * struct SomeTaskPolicy {
 *     using task_type = /* platform-specific task type */;
 *     
 *     // Create and start a new task with the given function
 *     template<typename Func>
 *     static task_type create_task(Func&& f);
 *     
 *     // Wait for task to complete (blocking)
 *     static void join_task(task_type& t);
 *     
 *     // Yield current task's time slice
 *     static void yield();
 *     
 *     // Sleep for specified microseconds
 *     static void sleep_us(size_t microseconds);
 * };
 * 
 * This interface ensures all task policies are compatible with FlowGraph.
 */

} // namespace cler