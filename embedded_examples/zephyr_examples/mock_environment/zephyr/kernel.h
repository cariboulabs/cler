// Mock Zephyr kernel.h for compilation testing
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock types
typedef struct k_thread {
    void* dummy;
} k_thread_t;

typedef struct k_sem {
    int count;
} k_sem_t;

typedef char k_thread_stack_t;

// Mock constants
#define K_NO_WAIT 0
#define K_FOREVER -1

// Mock functions
static inline int64_t k_uptime_get(void) {
    return 0;
}

static inline void k_yield(void) {
    // Mock implementation
}

static inline void k_usleep(uint32_t us) {
    // Mock implementation  
    (void)us;
}

static inline int k_sem_init(k_sem_t* sem, unsigned int initial_count, unsigned int limit) {
    (void)sem; (void)initial_count; (void)limit;
    return 0;
}

static inline int k_sem_take(k_sem_t* sem, int32_t timeout) {
    (void)sem; (void)timeout;
    return 0;
}

static inline void k_sem_give(k_sem_t* sem) {
    (void)sem;
}

static inline int k_thread_create(k_thread_t* thread, k_thread_stack_t* stack, 
                                  size_t stack_size, void (*entry)(void*, void*, void*),
                                  void* p1, void* p2, void* p3, int prio, 
                                  uint32_t options, int32_t delay) {
    (void)thread; (void)stack; (void)stack_size; (void)entry;
    (void)p1; (void)p2; (void)p3; (void)prio; (void)options; (void)delay;
    return 0;
}

#ifdef __cplusplus
}
#endif