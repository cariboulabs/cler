// Mock ThreadX tx_api.h for compilation testing
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock types
typedef struct TX_THREAD {
    void* dummy;
} TX_THREAD;

typedef struct TX_SEMAPHORE {
    int count;
} TX_SEMAPHORE;

typedef unsigned long ULONG;
typedef unsigned char UCHAR;
typedef unsigned int UINT;

// Mock constants
#define TX_SUCCESS 0
#define TX_AUTO_START 1
#define TX_NO_TIME_SLICE 0
#define TX_WAIT_FOREVER 0xFFFFFFFF

// Mock functions
static inline ULONG tx_time_get(void) {
    return 0;
}

static inline void tx_thread_relinquish(void) {
    // Mock implementation
}

static inline void tx_thread_sleep(ULONG timer_ticks) {
    (void)timer_ticks;
}

static inline UINT tx_semaphore_create(TX_SEMAPHORE* semaphore_ptr, 
                                       char* name_ptr, ULONG initial_count) {
    (void)semaphore_ptr; (void)name_ptr; (void)initial_count;
    return TX_SUCCESS;
}

static inline UINT tx_semaphore_get(TX_SEMAPHORE* semaphore_ptr, ULONG wait_option) {
    (void)semaphore_ptr; (void)wait_option;
    return TX_SUCCESS;
}

static inline UINT tx_semaphore_put(TX_SEMAPHORE* semaphore_ptr) {
    (void)semaphore_ptr;
    return TX_SUCCESS;
}

static inline UINT tx_semaphore_delete(TX_SEMAPHORE* semaphore_ptr) {
    (void)semaphore_ptr;
    return TX_SUCCESS;
}

static inline UINT tx_thread_create(TX_THREAD* thread_ptr, char* name_ptr,
                                    void (*entry_function)(ULONG), ULONG entry_input,
                                    void* stack_start, ULONG stack_size,
                                    UINT priority, UINT preempt_threshold,
                                    ULONG time_slice, UINT auto_start) {
    (void)thread_ptr; (void)name_ptr; (void)entry_function; (void)entry_input;
    (void)stack_start; (void)stack_size; (void)priority; (void)preempt_threshold;
    (void)time_slice; (void)auto_start;
    return TX_SUCCESS;
}

static inline UINT tx_thread_terminate(TX_THREAD* thread_ptr) {
    (void)thread_ptr;
    return TX_SUCCESS;
}

static inline UINT tx_thread_delete(TX_THREAD* thread_ptr) {
    (void)thread_ptr;
    return TX_SUCCESS;
}

static inline void tx_kernel_enter(void) {
    // Mock implementation
}

// Mock application define function pointer
extern void tx_application_define(void* first_unused_memory);

#ifdef __cplusplus
}
#endif