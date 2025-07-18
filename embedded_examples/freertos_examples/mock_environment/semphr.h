// Mock FreeRTOS semphr.h for running flowgraph examples
#pragma once

#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

// Mock semaphore structure
struct MockSemaphore {
    std::mutex mtx;
    std::condition_variable cv;
    bool available;
    
    MockSemaphore() : available(false) {}
};

// Mock semaphore functions
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    auto* sem = new MockSemaphore();
    return (SemaphoreHandle_t)sem;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    if (xSemaphore) {
        delete (MockSemaphore*)xSemaphore;
    }
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    if (!xSemaphore) return pdFAIL;
    
    auto* sem = (MockSemaphore*)xSemaphore;
    std::unique_lock<std::mutex> lock(sem->mtx);
    
    if (xTicksToWait == portMAX_DELAY) {
        // Wait indefinitely
        sem->cv.wait(lock, [sem] { return sem->available; });
        sem->available = false;
        return pdPASS;
    } else if (xTicksToWait == 0) {
        // Try without waiting
        if (sem->available) {
            sem->available = false;
            return pdPASS;
        }
        return pdFAIL;
    } else {
        // Wait with timeout
        auto timeout = std::chrono::milliseconds(xTicksToWait * 10);
        if (sem->cv.wait_for(lock, timeout, [sem] { return sem->available; })) {
            sem->available = false;
            return pdPASS;
        }
        return pdFAIL;
    }
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    if (!xSemaphore) return pdFAIL;
    
    auto* sem = (MockSemaphore*)xSemaphore;
    {
        std::lock_guard<std::mutex> lock(sem->mtx);
        sem->available = true;
    }
    sem->cv.notify_one();
    return pdPASS;
}

#ifdef __cplusplus
}
#endif