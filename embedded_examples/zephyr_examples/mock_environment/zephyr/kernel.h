// Mock Zephyr kernel.h for running flowgraph examples
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

#ifdef __cplusplus
extern "C" {
#endif

// Mock types
typedef struct k_thread {
    std::thread* thread_ptr;
    std::function<void(void*, void*, void*)>* entry_func;
    void* p1;
    void* p2;
    void* p3;
} k_thread_t;

typedef struct k_sem {
    std::mutex* mtx;
    std::condition_variable* cv;
    int count;
    int limit;
} k_sem_t;

typedef char k_thread_stack_t;

// Mock constants
#define K_NO_WAIT 0
#define K_FOREVER -1

// Mock functions
static inline int64_t k_uptime_get(void) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

static inline void k_yield(void) {
    std::this_thread::yield();
}

static inline void k_usleep(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

static inline int k_sem_init(k_sem_t* sem, unsigned int initial_count, unsigned int limit) {
    if (!sem) return -1;
    
    sem->mtx = new std::mutex();
    sem->cv = new std::condition_variable();
    sem->count = initial_count;
    sem->limit = limit;
    return 0;
}

static inline int k_sem_take(k_sem_t* sem, int32_t timeout) {
    if (!sem || !sem->mtx || !sem->cv) return -1;
    
    std::unique_lock<std::mutex> lock(*sem->mtx);
    
    if (timeout == K_FOREVER) {
        sem->cv->wait(lock, [sem] { return sem->count > 0; });
        sem->count--;
        return 0;
    } else if (timeout == K_NO_WAIT) {
        if (sem->count > 0) {
            sem->count--;
            return 0;
        }
        return -1;
    } else {
        auto timeout_duration = std::chrono::milliseconds(timeout);
        if (sem->cv->wait_for(lock, timeout_duration, [sem] { return sem->count > 0; })) {
            sem->count--;
            return 0;
        }
        return -1;
    }
}

static inline void k_sem_give(k_sem_t* sem) {
    if (!sem || !sem->mtx || !sem->cv) return;
    
    {
        std::lock_guard<std::mutex> lock(*sem->mtx);
        if (sem->count < sem->limit) {
            sem->count++;
        }
    }
    sem->cv->notify_one();
}

static inline int k_thread_create(k_thread_t* thread, k_thread_stack_t* stack, 
                                  size_t stack_size, void (*entry)(void*, void*, void*),
                                  void* p1, void* p2, void* p3, int prio, 
                                  uint32_t options, int32_t delay) {
    (void)stack; (void)stack_size; (void)prio; (void)options;
    
    if (!thread || !entry) return -1;
    
    thread->entry_func = new std::function<void(void*, void*, void*)>(entry);
    thread->p1 = p1;
    thread->p2 = p2;
    thread->p3 = p3;
    
    if (delay == K_NO_WAIT) {
        thread->thread_ptr = new std::thread([thread]() {
            (*thread->entry_func)(thread->p1, thread->p2, thread->p3);
        });
    } else {
        // For delayed start, we'd need to implement a timer mechanism
        // For now, just start immediately
        thread->thread_ptr = new std::thread([thread, delay]() {
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
            (*thread->entry_func)(thread->p1, thread->p2, thread->p3);
        });
    }
    
    return 0;
}

#ifdef __cplusplus
}
#endif