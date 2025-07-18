// Mock ThreadX tx_api.h for running flowgraph examples
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <functional>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare types
typedef unsigned long ULONG;
typedef unsigned char UCHAR;
typedef unsigned int UINT;

// Mock types
typedef struct TX_THREAD {
    std::thread* thread_ptr;
    std::function<void(ULONG)>* entry_func;
    ULONG entry_input;
} TX_THREAD;

typedef struct TX_SEMAPHORE {
    std::mutex* mtx;
    std::condition_variable* cv;
    int count;
} TX_SEMAPHORE;

// Mock constants
#define TX_SUCCESS 0
#define TX_AUTO_START 1
#define TX_NO_TIME_SLICE 0
#define TX_WAIT_FOREVER 0xFFFFFFFF

// Mock functions
static inline ULONG tx_time_get(void) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

static inline void tx_thread_relinquish(void) {
    std::this_thread::yield();
}

static inline void tx_thread_sleep(ULONG timer_ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timer_ticks));
}

static inline UINT tx_semaphore_create(TX_SEMAPHORE* semaphore_ptr, 
                                       char* name_ptr, ULONG initial_count) {
    (void)name_ptr;
    if (!semaphore_ptr) return 1;
    
    semaphore_ptr->mtx = new std::mutex();
    semaphore_ptr->cv = new std::condition_variable();
    semaphore_ptr->count = initial_count;
    return TX_SUCCESS;
}

static inline UINT tx_semaphore_get(TX_SEMAPHORE* semaphore_ptr, ULONG wait_option) {
    if (!semaphore_ptr || !semaphore_ptr->mtx || !semaphore_ptr->cv) return 1;
    
    std::unique_lock<std::mutex> lock(*semaphore_ptr->mtx);
    
    if (wait_option == TX_WAIT_FOREVER) {
        semaphore_ptr->cv->wait(lock, [semaphore_ptr] { return semaphore_ptr->count > 0; });
        semaphore_ptr->count--;
        return TX_SUCCESS;
    } else if (wait_option == 0) {
        if (semaphore_ptr->count > 0) {
            semaphore_ptr->count--;
            return TX_SUCCESS;
        }
        return 1;
    } else {
        auto timeout = std::chrono::milliseconds(wait_option);
        if (semaphore_ptr->cv->wait_for(lock, timeout, [semaphore_ptr] { return semaphore_ptr->count > 0; })) {
            semaphore_ptr->count--;
            return TX_SUCCESS;
        }
        return 1;
    }
}

static inline UINT tx_semaphore_put(TX_SEMAPHORE* semaphore_ptr) {
    if (!semaphore_ptr || !semaphore_ptr->mtx || !semaphore_ptr->cv) return 1;
    
    {
        std::lock_guard<std::mutex> lock(*semaphore_ptr->mtx);
        semaphore_ptr->count++;
    }
    semaphore_ptr->cv->notify_one();
    return TX_SUCCESS;
}

static inline UINT tx_semaphore_delete(TX_SEMAPHORE* semaphore_ptr) {
    if (!semaphore_ptr) return 1;
    
    if (semaphore_ptr->mtx) {
        delete semaphore_ptr->mtx;
        semaphore_ptr->mtx = nullptr;
    }
    if (semaphore_ptr->cv) {
        delete semaphore_ptr->cv;
        semaphore_ptr->cv = nullptr;
    }
    return TX_SUCCESS;
}

static inline UINT tx_thread_create(TX_THREAD* thread_ptr, char* name_ptr,
                                    void (*entry_function)(ULONG), ULONG entry_input,
                                    void* stack_start, ULONG stack_size,
                                    UINT priority, UINT preempt_threshold,
                                    ULONG time_slice, UINT auto_start) {
    (void)name_ptr; (void)stack_start; (void)stack_size;
    (void)priority; (void)preempt_threshold; (void)time_slice;
    
    if (!thread_ptr || !entry_function) return 1;
    
    thread_ptr->entry_input = entry_input;
    thread_ptr->entry_func = new std::function<void(ULONG)>(entry_function);
    
    if (auto_start == TX_AUTO_START) {
        thread_ptr->thread_ptr = new std::thread([thread_ptr]() {
            (*thread_ptr->entry_func)(thread_ptr->entry_input);
        });
    }
    
    return TX_SUCCESS;
}

static inline UINT tx_thread_terminate(TX_THREAD* thread_ptr) {
    // In mock, we can't really terminate a std::thread
    // Just mark it as done
    (void)thread_ptr;
    return TX_SUCCESS;
}

static inline UINT tx_thread_delete(TX_THREAD* thread_ptr) {
    if (!thread_ptr) return 1;
    
    if (thread_ptr->thread_ptr) {
        if (thread_ptr->thread_ptr->joinable()) {
            thread_ptr->thread_ptr->join();
        }
        delete thread_ptr->thread_ptr;
        thread_ptr->thread_ptr = nullptr;
    }
    
    if (thread_ptr->entry_func) {
        delete thread_ptr->entry_func;
        thread_ptr->entry_func = nullptr;
    }
    
    return TX_SUCCESS;
}

static inline void tx_kernel_enter(void) {
    // Call application define
    extern void tx_application_define(void* first_unused_memory);
    tx_application_define(nullptr);
    
    // In mock environment, just wait forever
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

#ifdef __cplusplus
}
#endif