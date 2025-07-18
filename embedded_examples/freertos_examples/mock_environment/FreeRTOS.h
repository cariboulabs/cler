// Mock FreeRTOS.h for running flowgraph examples
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif

// --- Mock types ---
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

// --- Mock constants ---
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFF
#define tskIDLE_PRIORITY 0

// --- Mock macros ---
#define pdMS_TO_TICKS(ms) ((ms) / 10)  // Mock conversion (10ms per tick)

// --- Internal task wrapper ---
struct MockTask {
    std::thread thread;
    std::function<void(void*)> taskFunc;
    void* params;

    ~MockTask() {
        if (thread.joinable()) {
            thread.detach();  // avoid resource leaks
        }
    }
};

// --- Tick count mock ---
static inline TickType_t xTaskGetTickCount(void) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 10;
}

// --- Task creation ---
static inline BaseType_t xTaskCreate(
    void (*taskCode)(void*),
    const char* const pcName,
    const uint16_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask)
{
    (void)pcName;
    (void)usStackDepth;
    (void)uxPriority;

    try {
        auto* task = new MockTask();
        task->taskFunc = taskCode;
        task->params = pvParameters;
        task->thread = std::thread([task]() {
            task->taskFunc(task->params);
        });

        if (pxCreatedTask) {
            *pxCreatedTask = (TaskHandle_t)task;
        }
    } catch (...) {
        return pdFAIL;
    }

    return pdPASS;
}

// --- Scheduler startup ---
static inline void vTaskStartScheduler(void) {
    // Threads already running in host environment
    // Simulate idle scheduler loop
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// --- Task deletion ---
static inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
    if (xTaskToDelete == NULL) {
        // Simulate "delete current task" by returning from thread
        return;
    }

    // Cleanup mock task memory — thread will be detached in destructor
    auto* task = static_cast<MockTask*>(xTaskToDelete);
    delete task;
}

// --- Task delay ---
static inline void vTaskDelay(const TickType_t xTicksToDelay) {
    std::this_thread::sleep_for(std::chrono::milliseconds(xTicksToDelay * 10));
}

// --- Yield current task ---
static inline void taskYIELD(void) {
    std::this_thread::yield();
}

#ifdef __cplusplus
}
#endif
