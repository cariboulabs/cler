#pragma once

#include "cler.hpp"

// Include FreeRTOS headers
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

namespace cler {

/**
 * FreeRTOS Threading Policy for CLER FlowGraph
 * 
 * Usage:
 * 1. Include this file after including FreeRTOS headers
 * 2. Use with FlowGraph template parameter:
 *    FlowGraph<FreeRTOSThreadPolicy, BlockRunner<...>, ...> flowgraph(...);
 * 3. Call flowgraph.run() from a FreeRTOS task or before starting the scheduler
 * 
 * Requirements:
 * - FreeRTOS kernel must be running
 * - Sufficient heap for task stacks (configurable via CLER_FREERTOS_STACK_SIZE)
 * - Task priority can be set via CLER_FREERTOS_PRIORITY
 * 
 * Configuration (define before including this file):
 * #define CLER_FREERTOS_STACK_SIZE    2048    // Stack size in words
 * #define CLER_FREERTOS_PRIORITY      (tskIDLE_PRIORITY + 1)  // Task priority
 */

#ifndef CLER_FREERTOS_STACK_SIZE
#define CLER_FREERTOS_STACK_SIZE 2048
#endif

#ifndef CLER_FREERTOS_PRIORITY
#define CLER_FREERTOS_PRIORITY (tskIDLE_PRIORITY + 1)
#endif

struct FreeRTOSThreadPolicy {
    struct TaskWrapper {
        std::function<void()>* task_function;
        TaskHandle_t handle;
        SemaphoreHandle_t completion_sem;
        volatile bool should_stop;
    };
    
    using thread_type = TaskWrapper;
    
    template<typename Func>
    static thread_type create_thread(Func&& f) {
        thread_type wrapper{};
        wrapper.should_stop = false;
        
        // Store the function in dynamically allocated memory
        wrapper.task_function = new std::function<void()>(std::forward<Func>(f));
        
        // Create completion semaphore
        wrapper.completion_sem = xSemaphoreCreateBinary();
        if (wrapper.completion_sem == nullptr) {
            delete wrapper.task_function;
            wrapper.task_function = nullptr;
            return wrapper;
        }
        
        // Create FreeRTOS task
        BaseType_t result = xTaskCreate(
            task_entry_point,           // Task function
            "ClerTask",                 // Task name
            CLER_FREERTOS_STACK_SIZE,   // Stack size in words
            &wrapper,                   // Parameters
            CLER_FREERTOS_PRIORITY,     // Priority
            &wrapper.handle             // Task handle
        );
        
        if (result != pdPASS) {
            delete wrapper.task_function;
            vSemaphoreDelete(wrapper.completion_sem);
            wrapper.task_function = nullptr;
            wrapper.completion_sem = nullptr;
            wrapper.handle = nullptr;
        }
        
        return wrapper;
    }
    
    static void join_thread(thread_type& wrapper) {
        if (wrapper.task_function && wrapper.handle) {
            // Signal task to stop
            wrapper.should_stop = true;
            
            // Wait for completion
            xSemaphoreTake(wrapper.completion_sem, portMAX_DELAY);
            
            // Clean up
            delete wrapper.task_function;
            vSemaphoreDelete(wrapper.completion_sem);
            wrapper.task_function = nullptr;
            wrapper.completion_sem = nullptr;
            wrapper.handle = nullptr;
        }
    }
    
    static void yield() {
        taskYIELD();
    }
    
    static void sleep_us(size_t microseconds) {
        // Convert microseconds to ticks
        TickType_t ticks = pdMS_TO_TICKS(microseconds / 1000);
        if (ticks == 0) ticks = 1; // Minimum delay of 1 tick
        vTaskDelay(ticks);
    }

private:
    // Task entry point for FreeRTOS
    static void task_entry_point(void* parameters) {
        auto* wrapper = static_cast<TaskWrapper*>(parameters);
        
        // Execute the stored function
        if (wrapper->task_function) {
            (*wrapper->task_function)();
        }
        
        // Signal completion
        xSemaphoreGive(wrapper->completion_sem);
        
        // Delete the task
        vTaskDelete(NULL);
    }
};

// Convenient alias for FreeRTOS-based FlowGraph
template<typename... BlockRunners>
using FreeRTOSFlowGraph = FlowGraph<FreeRTOSThreadPolicy, BlockRunners...>;

} // namespace cler

/*
Example usage:

#include "FreeRTOS.h"
#include "task.h"
#include "cler.hpp"
#include "cler_freertos_policy.hpp"

void app_main(void* parameters) {
    // Your blocks
    MySourceBlock source("Source");
    MyProcessBlock processor("Processor");
    MySinkBlock sink("Sink");
    
    // Channels
    cler::Channel<float, 1024> ch1;
    cler::Channel<float, 1024> ch2;
    
    // Create FreeRTOS FlowGraph
    cler::FreeRTOSFlowGraph flowgraph(
        cler::BlockRunner(&source, &ch1),
        cler::BlockRunner(&processor, &ch1, &ch2),
        cler::BlockRunner(&sink, &ch2)
    );
    
    flowgraph.run();
    
    // Let it run for some time or until external stop condition
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    flowgraph.stop();
    vTaskDelete(NULL);
}

int main() {
    // Create the main application task
    xTaskCreate(app_main, "AppMain", 4096, NULL, 1, NULL);
    
    // Start FreeRTOS scheduler
    vTaskStartScheduler();
    
    return 0;
}
*/