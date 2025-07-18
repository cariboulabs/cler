// Mock FreeRTOS semphr.h for running flowgraph examples
#pragma once

#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* SemaphoreHandle_t;

#ifdef __cplusplus
}
#endif

// Mock semaphore structure
struct MockSemaphore {
    std::mutex mtx;
    std::condition_variable cv;
    bool available = false;
};

// --- Semaphore Functions ---
#ifdef __cplusplus
extern "C" {
#endif

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return reinterpret_cast<SemaphoreHandle_t>(new MockSemaphore());
}

static inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    delete reinterpret_cast<MockSemaphore*>(xSemaphore);
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    if (!xSemaphore) return pdFAIL;
    
    MockSemaphore* sem = reinterpret_cast<MockSemaphore*>(xSemaphore);
    std::unique_lock<std::mutex> lock(sem->mtx);

    if (xTicksToWait == 0) {
        if (sem->available) {
            sem->available = false;
            return pdPASS;
        }
        return pdFAIL;
    }

    if (xTicksToWait == portMAX_DELAY) {
        sem->cv.wait(lock, [&] { return sem->available; });
        sem->available = false;
        return pdPASS;
    }

    auto timeout = std::chrono::milliseconds(xTicksToWait * 10);
    if (sem->cv.wait_for(lock, timeout, [&] { return sem->available; })) {
        sem->available = false;
        return pdPASS;
    }

    return pdFAIL;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    if (!xSemaphore) return pdFAIL;

    MockSemaphore* sem = reinterpret_cast<MockSemaphore*>(xSemaphore);
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
