#pragma once

#include "cler.hpp"

// Include ThreadX headers
#include "tx_api.h"

namespace cler {

/**
 * ThreadX Threading Policy for CLER FlowGraph
 * 
 * Usage:
 * 1. Include this file after including ThreadX headers
 * 2. Use with FlowGraph template parameter:
 *    FlowGraph<ThreadXThreadPolicy, BlockRunner<...>, ...> flowgraph(...);
 * 3. Call flowgraph.run() from within a ThreadX application
 * 
 * Requirements:
 * - ThreadX kernel must be initialized (tx_kernel_enter() called)
 * - Sufficient memory for thread stacks (configurable via CLER_THREADX_STACK_SIZE)
 * - Thread priority can be set via CLER_THREADX_PRIORITY
 * 
 * Configuration (define before including this file):
 * #define CLER_THREADX_STACK_SIZE     4096    // Stack size in bytes
 * #define CLER_THREADX_PRIORITY       16      // Thread priority (1-31)
 * #define CLER_THREADX_TIME_SLICE     TX_NO_TIME_SLICE  // Time slice
 * #define CLER_THREADX_PREEMPT_THRESHOLD  16  // Preemption threshold
 */

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

struct ThreadXThreadPolicy {
    struct ThreadWrapper {
        TX_THREAD thread;
        TX_SEMAPHORE completion_sem;
        std::function<void()>* thread_function;
        UCHAR* stack_memory;
        volatile bool should_stop;
        bool is_valid;
    };
    
    using thread_type = ThreadWrapper;
    
    template<typename Func>
    static thread_type create_thread(Func&& f) {
        thread_type wrapper{};
        wrapper.is_valid = false;
        wrapper.should_stop = false;
        
        // Allocate stack memory
        wrapper.stack_memory = new UCHAR[CLER_THREADX_STACK_SIZE];
        if (!wrapper.stack_memory) {
            return wrapper; // Failed allocation
        }
        
        // Store the function in dynamically allocated memory
        wrapper.thread_function = new std::function<void()>(std::forward<Func>(f));
        
        // Create completion semaphore
        UINT status = tx_semaphore_create(
            &wrapper.completion_sem,
            "ClerCompletionSem",
            0  // Initial count
        );
        
        if (status != TX_SUCCESS) {
            delete wrapper.thread_function;
            delete[] wrapper.stack_memory;
            return wrapper;
        }
        
        // Create ThreadX thread
        status = tx_thread_create(
            &wrapper.thread,                    // Thread control block
            "ClerThread",                       // Thread name
            thread_entry_point,                 // Thread entry function
            (ULONG)&wrapper,                   // Thread input
            wrapper.stack_memory,               // Stack start
            CLER_THREADX_STACK_SIZE,           // Stack size
            CLER_THREADX_PRIORITY,             // Priority
            CLER_THREADX_PREEMPT_THRESHOLD,    // Preemption threshold
            CLER_THREADX_TIME_SLICE,           // Time slice
            TX_AUTO_START                       // Auto start
        );
        
        if (status != TX_SUCCESS) {
            tx_semaphore_delete(&wrapper.completion_sem);
            delete wrapper.thread_function;
            delete[] wrapper.stack_memory;
            return wrapper;
        }
        
        wrapper.is_valid = true;
        return wrapper;
    }
    
    static void join_thread(thread_type& wrapper) {
        if (wrapper.is_valid && wrapper.thread_function) {
            // Signal thread to stop
            wrapper.should_stop = true;
            
            // Wait for thread completion
            tx_semaphore_get(&wrapper.completion_sem, TX_WAIT_FOREVER);
            
            // Clean up thread
            tx_thread_terminate(&wrapper.thread);
            tx_thread_delete(&wrapper.thread);
            tx_semaphore_delete(&wrapper.completion_sem);
            
            // Clean up memory
            delete wrapper.thread_function;
            delete[] wrapper.stack_memory;
            
            wrapper.thread_function = nullptr;
            wrapper.stack_memory = nullptr;
            wrapper.is_valid = false;
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
        auto* wrapper = reinterpret_cast<ThreadWrapper*>(parameters);
        
        // Execute the stored function
        if (wrapper->thread_function) {
            (*wrapper->thread_function)();
        }
        
        // Signal completion
        tx_semaphore_put(&wrapper->completion_sem);
        
        // Thread will be cleaned up by join_thread
    }
};

// Convenient alias for ThreadX-based FlowGraph
template<typename... BlockRunners>
using ThreadXFlowGraph = FlowGraph<ThreadXThreadPolicy, BlockRunners...>;

} // namespace cler

/*
Example usage:

#include "tx_api.h"
#include "cler.hpp"
#include "cler_threadx_policy.hpp"

// Define memory pool for ThreadX
#define DEMO_STACK_SIZE         1024
#define DEMO_BYTE_POOL_SIZE     9120
#define DEMO_BLOCK_POOL_SIZE    100

UCHAR memory_area[DEMO_BYTE_POOL_SIZE];
TX_BYTE_POOL byte_pool_0;

void tx_application_define(void *first_unused_memory) {
    CHAR *pointer = TX_NULL;
    
    // Create a byte memory pool
    tx_byte_pool_create(&byte_pool_0, "byte pool 0", memory_area, DEMO_BYTE_POOL_SIZE);
    
    // Allocate the stack for the main application thread
    tx_byte_allocate(&byte_pool_0, (VOID **) &pointer, DEMO_STACK_SIZE, TX_NO_WAIT);
    
    // Create the main application thread
    tx_thread_create(&main_thread, "main thread", main_thread_entry, 0,
                     pointer, DEMO_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
}

void main_thread_entry(ULONG thread_input) {
    // Your blocks
    MySourceBlock source("Source");
    MyProcessBlock processor("Processor");  
    MySinkBlock sink("Sink");
    
    // Channels (using static allocation for embedded)
    cler::Channel<float, 1024> ch1;
    cler::Channel<float, 1024> ch2;
    
    // Create ThreadX FlowGraph
    cler::ThreadXFlowGraph flowgraph(
        cler::BlockRunner(&source, &ch1),
        cler::BlockRunner(&processor, &ch1, &ch2),
        cler::BlockRunner(&sink, &ch2)
    );
    
    flowgraph.run();
    
    // Let it run for some time
    tx_thread_sleep(1000); // 10 seconds assuming 100 ticks/sec
    
    flowgraph.stop();
}

int main() {
    // Enter the ThreadX kernel
    tx_kernel_enter();
    return 0;
}
*/