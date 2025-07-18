// Mock Zephyr kernel.h for running flowgraph examples
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

// --- Mock types ---
typedef struct k_thread {
    std::thread* thread_ptr;
    std::function<void(void*, void*, void*)>* entry_func;
    void* p1;
    void* p2;
    void* p3;
} k_thread_t;

typedef struct k_sem {
    std::mutex* mtx = nullptr;
    std::condition_variable* cv = nullptr;
    int count = 0;
    int limit = 0;
} k_sem_t;

typedef char k_thread_stack_t;

// --- Mock constants ---
#define K_NO_WAIT 0
#define K_FOREVER -1

// --- Time functions ---
static inline int64_t k_uptime_get(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static inline void k_yield(void) {
    std::this_thread::yield();
}

static inline void k_usleep(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

// --- Semaphore API ---
static inline int k_sem_init(k_sem_t* sem, unsigned int initial_count, unsigned int limit) {
    if (!sem) return -1;
    sem->mtx = new std::mutex();
    sem->cv = new std::condition_variable();
    sem->count = initial_count;
    sem->limit = limit;
    return 0;
}

static inline void k_sem_reset(k_sem_t* sem) {
    if (sem && sem->mtx) {
        std::lock_guard<std::mutex> lock(*sem->mtx);
        sem->count = 0;
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

static inline int k_sem_take(k_sem_t* sem, int32_t timeout) {
    if (!sem || !sem->mtx || !sem->cv) return -1;

    std::unique_lock<std::mutex> lock(*sem->mtx);
    if (timeout == K_NO_WAIT) {
        if (sem->count > 0) {
            --sem->count;
            return 0;
        }
        return -1;
    } else if (timeout == K_FOREVER) {
        sem->cv->wait(lock, [sem] { return sem->count > 0; });
        --sem->count;
        return 0;
    } else {
        auto duration = std::chrono::milliseconds(timeout);
        if (sem->cv->wait_for(lock, duration, [sem] { return sem->count > 0; })) {
            --sem->count;
            return 0;
        }
        return -1;
    }
}

static inline void k_sem_deinit(k_sem_t* sem) {
    if (!sem) return;
    delete sem->mtx;
    delete sem->cv;
    sem->mtx = nullptr;
    sem->cv = nullptr;
    sem->count = 0;
    sem->limit = 0;
}

// --- Thread API ---
static inline int k_thread_create(k_thread_t* thread, k_thread_stack_t* stack, 
                                  size_t stack_size, void (*entry)(void*, void*, void*),
                                  void* p1, void* p2, void* p3, int prio, 
                                  uint32_t options, int32_t delay) {
    (void)stack; (void)stack_size;
    (void)prio;  (void)options;

    if (!thread || !entry) return -1;

    thread->entry_func = new std::function<void(void*, void*, void*)>(entry);
    thread->p1 = p1;
    thread->p2 = p2;
    thread->p3 = p3;

    thread->thread_ptr = new std::thread([thread, delay]() {
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        (*thread->entry_func)(thread->p1, thread->p2, thread->p3);
    });

    return 0;
}

static inline void k_thread_join_and_destroy(k_thread_t* thread) {
    if (!thread) return;
    if (thread->thread_ptr) {
        if (thread->thread_ptr->joinable()) {
            thread->thread_ptr->join();
        }
        delete thread->thread_ptr;
        thread->thread_ptr = nullptr;
    }
    delete thread->entry_func;
    thread->entry_func = nullptr;
}

#ifdef __cplusplus
}
#endif
