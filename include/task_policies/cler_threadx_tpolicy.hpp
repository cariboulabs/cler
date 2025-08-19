#pragma once

#include "cler_task_policy_base.hpp"
// Include ThreadX headers
#include "tx_api.h"

namespace cler {

#ifndef CLER_THREADX_STACK_SIZE
#define CLER_THREADX_STACK_SIZE 4096
#endif

#ifndef CLER_THREADX_PRIORITY
#define CLER_THREADX_PRIORITY 16
#endif

#ifndef CLER_THREADX_TIME_SLICE
#define CLER_THREADX_TIME_SLICE TX_NO_TIME_SLICE
#endif

#ifndef CLER_THREADX_PREEMPT_THRESHOLD
#define CLER_THREADX_PREEMPT_THRESHOLD 16
#endif

struct ThreadXTaskPolicy : TaskPolicyBase<ThreadXTaskPolicy> {
    // Import default implementations from base
    using TaskPolicyBase<ThreadXTaskPolicy>::relax;
    using TaskPolicyBase<ThreadXTaskPolicy>::pin_to_core;
    
    // Structure to hold task data
    struct TaskData {
        void* callable;
        void (*invoke)(void*);
        void (*destroy)(void*);
        TX_SEMAPHORE* completion_sem;
        volatile bool task_completed;
    };
    
    struct TaskWrapper {
        TX_THREAD thread;
        TX_SEMAPHORE completion_sem;
        TaskData* task_data;
        UCHAR* stack_memory;
        volatile bool task_completed;
        bool is_valid;
    };
    
    using task_type = TaskWrapper;
    
    // Template helper to generate invoke and destroy functions
    template<typename Func>
    struct TaskHelper {
        static void invoke(void* callable) {
            (*static_cast<Func*>(callable))();
        }
        
        static void destroy(void* callable) {
            delete static_cast<Func*>(callable);
        }
    };
    
    template<typename Func>
    static task_type create_task(Func&& f) {
        task_type wrapper{};
        wrapper.is_valid = false;
        wrapper.task_completed = false;
        wrapper.task_data = nullptr;
        
        // Allocate stack memory
        wrapper.stack_memory = new UCHAR[CLER_THREADX_STACK_SIZE];
        if (!wrapper.stack_memory) {
            return wrapper; // Failed allocation
        }
        
        // Create completion semaphore
        static char sem_name[] = "ClerCompletionSem";
        UINT status = tx_semaphore_create(
            &wrapper.completion_sem,
            sem_name,
            0  // Initial count
        );
        
        if (status != TX_SUCCESS) {
            delete[] wrapper.stack_memory;
            return wrapper;
        }
        
        // Allocate task data
        using FuncType = typename std::decay<Func>::type;
        auto* task_data = new TaskData;
        task_data->callable = new FuncType(std::forward<Func>(f));
        task_data->invoke = &TaskHelper<FuncType>::invoke;
        task_data->destroy = &TaskHelper<FuncType>::destroy;
        task_data->completion_sem = &wrapper.completion_sem;
        task_data->task_completed = false;
        wrapper.task_data = task_data;
        
        // Create ThreadX thread
        static char thread_name[] = "ClerThread";
        status = tx_thread_create(
            &wrapper.thread,                    // Thread control block
            thread_name,                        // Thread name
            thread_entry_point,                 // Thread entry function
            (ULONG)task_data,                  // Thread input
            wrapper.stack_memory,               // Stack start
            CLER_THREADX_STACK_SIZE,           // Stack size
            CLER_THREADX_PRIORITY,             // Priority
            CLER_THREADX_PREEMPT_THRESHOLD,    // Preemption threshold
            CLER_THREADX_TIME_SLICE,           // Time slice
            TX_AUTO_START                       // Auto start
        );
        
        if (status != TX_SUCCESS) {
            tx_semaphore_delete(&wrapper.completion_sem);
            task_data->destroy(task_data->callable);
            delete task_data;
            delete[] wrapper.stack_memory;
            wrapper.task_data = nullptr;
            return wrapper;
        }
        
        wrapper.is_valid = true;
        return wrapper;
    }
    
    static void join_task(task_type& wrapper) {
        if (wrapper.is_valid && wrapper.task_data && !wrapper.task_completed) {
            // Wait for thread completion
            tx_semaphore_get(&wrapper.completion_sem, TX_WAIT_FOREVER);
            
            // Clean up thread
            tx_thread_terminate(&wrapper.thread);
            tx_thread_delete(&wrapper.thread);
            tx_semaphore_delete(&wrapper.completion_sem);
            
            // Clean up memory
            wrapper.task_data->destroy(wrapper.task_data->callable);
            delete wrapper.task_data;
            delete[] wrapper.stack_memory;
            
            wrapper.task_data = nullptr;
            wrapper.stack_memory = nullptr;
            wrapper.is_valid = false;
            wrapper.task_completed = true;
        }
    }
    
    static void yield() {
        tx_thread_relinquish();
    }
    
    static void sleep_us(size_t microseconds) {
        // Convert microseconds to timer ticks
        // ThreadX timer ticks are typically milliseconds
        ULONG ticks = (microseconds + 999) / 1000; // Round up to ms
        if (ticks == 0) ticks = 1; // Minimum delay
        tx_thread_sleep(ticks);
    }

private:
    // Thread entry point for ThreadX
    static void thread_entry_point(ULONG parameters) {
        auto* data = reinterpret_cast<TaskData*>(parameters);
        
        // Execute the stored function
        if (data->callable && data->invoke) {
            data->invoke(data->callable);
        }
        
        // Signal completion
        tx_semaphore_put(data->completion_sem);
        
        // Thread will be cleaned up by join_task
    }
};

// Forward declaration
template<typename TaskPolicy, typename... BlockRunners>
class FlowGraph;

// Convenient factory function for ThreadX-based FlowGraph
template<typename... Runners>
auto make_threadx_flowgraph(Runners&&... runners) {
    return cler::FlowGraph<cler::ThreadXTaskPolicy, std::decay_t<Runners>...>(
        std::forward<Runners>(runners)...
    );
}

// Convenient alias for ThreadX-based FlowGraph
template<typename... BlockRunners>
using ThreadXFlowGraph = FlowGraph<ThreadXTaskPolicy, BlockRunners...>;

} // namespace cler