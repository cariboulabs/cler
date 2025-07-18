// Mock FreeRTOS semphr.h for compilation testing
#pragma once

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mock semaphore functions
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)0x1234;  // Mock handle
}

static inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    (void)xSemaphore;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    (void)xSemaphore; (void)xTicksToWait;
    return pdPASS;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    (void)xSemaphore;
    return pdPASS;
}

#ifdef __cplusplus
}
#endif