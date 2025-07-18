// Mock FreeRTOS.h for compilation testing
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock types
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

// Mock constants
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFF
#define tskIDLE_PRIORITY 0

// Mock macros
#define pdMS_TO_TICKS(ms) ((ms) / 10)  // Mock conversion

// Mock functions
static inline TickType_t xTaskGetTickCount(void) {
    return 0;
}

static inline BaseType_t xTaskCreate(
    void (*taskCode)(void*),
    const char* const pcName,
    const uint16_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask) {
    (void)taskCode; (void)pcName; (void)usStackDepth; 
    (void)pvParameters; (void)uxPriority; (void)pxCreatedTask;
    return pdPASS;
}

static inline void vTaskStartScheduler(void) {
    // Mock implementation
}

static inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
    (void)xTaskToDelete;
}

static inline void vTaskDelay(const TickType_t xTicksToDelay) {
    (void)xTicksToDelay;
}

static inline void taskYIELD(void) {
    // Mock implementation
}

#ifdef __cplusplus
}
#endif