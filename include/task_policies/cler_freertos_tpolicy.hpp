#pragma once

#include "cler_task_policy_base.hpp"
// Include FreeRTOS headers
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

namespace cler {

/**
 * FreeRTOS Task Policy for CLER FlowGraph
 * 
 * Usage:
 * 1. Include this file after including FreeRTOS headers
 * 2. Use with FlowGraph template parameter:
 *    FlowGraph<FreeRTOSTaskPolicy, BlockRunner<...>, ...> flowgraph(...);
 *    Or use the convenient alias:
 *    FreeRTOSFlowGraph<BlockRunner<...>, ...> flowgraph(...);
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

struct FreeRTOSTaskPolicy : TaskPolicyBase<FreeRTOSTaskPolicy> {
    // Structure to hold task data
    struct TaskData {
        void* callable;
        void (*invoke)(void*);
        void (*destroy)(void*);
        SemaphoreHandle_t completion_sem;
        volatile bool task_completed;
    };
    
    struct TaskWrapper {
        TaskHandle_t handle;
        SemaphoreHandle_t completion_sem;
        TaskData* task_data;
        volatile bool task_completed;
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
        wrapper.task_completed = false;
        wrapper.handle = nullptr;
        wrapper.task_data = nullptr;
        
        // Create completion semaphore
        wrapper.completion_sem = xSemaphoreCreateBinary();
        if (wrapper.completion_sem == nullptr) {
            return wrapper;
        }
        
        // Allocate task data
        using FuncType = typename std::decay<Func>::type;
        auto* task_data = new TaskData;
        task_data->callable = new FuncType(std::forward<Func>(f));
        task_data->invoke = &TaskHelper<FuncType>::invoke;
        task_data->destroy = &TaskHelper<FuncType>::destroy;
        task_data->completion_sem = wrapper.completion_sem;
        task_data->task_completed = false;
        wrapper.task_data = task_data;
        
        // Create FreeRTOS task
        BaseType_t result = xTaskCreate(
            task_entry_point,           // Task function
            "ClerTask",                 // Task name
            CLER_FREERTOS_STACK_SIZE,   // Stack size in words
            task_data,                  // Parameters
            CLER_FREERTOS_PRIORITY,     // Priority
            &wrapper.handle             // Task handle
        );
        
        if (result != pdPASS) {
            // Cleanup on failure
            vSemaphoreDelete(wrapper.completion_sem);
            task_data->destroy(task_data->callable);
            delete task_data;
            wrapper.completion_sem = nullptr;
            wrapper.handle = nullptr;
            wrapper.task_data = nullptr;
        }
        
        return wrapper;
    }
    
    static void join_task(task_type& wrapper) {
        if (wrapper.task_data && wrapper.handle && !wrapper.task_completed) {
            // Wait for completion
            xSemaphoreTake(wrapper.completion_sem, portMAX_DELAY);
            
            // Clean up
            wrapper.task_data->destroy(wrapper.task_data->callable);
            delete wrapper.task_data;
            vSemaphoreDelete(wrapper.completion_sem);
            wrapper.task_data = nullptr;
            wrapper.completion_sem = nullptr;
            wrapper.handle = nullptr;
            wrapper.task_completed = true;
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
        auto* data = static_cast<TaskData*>(parameters);
        
        // Execute the stored function
        if (data->callable && data->invoke) {
            data->invoke(data->callable);
        }
        
        // Signal completion
        xSemaphoreGive(data->completion_sem);
        
        // Delete the task
        vTaskDelete(NULL);
    }
};

// Forward declaration
template<typename TaskPolicy, typename... BlockRunners>
class FlowGraph;

// Convenient factory function for FreeRTOS-based FlowGraph
template<typename... Runners>
auto make_freertos_flowgraph(Runners&&... runners) {
    return cler::FlowGraph<cler::FreeRTOSTaskPolicy, std::decay_t<Runners>...>(
        std::forward<Runners>(runners)...
    );
}

// Convenient alias for FreeRTOS-based FlowGraph
template<typename... BlockRunners>
using FreeRTOSFlowGraph = FlowGraph<FreeRTOSTaskPolicy, BlockRunners...>;

} // namespace cler