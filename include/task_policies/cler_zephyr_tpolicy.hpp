#pragma once

#include "cler_task_policy_base.hpp"
#include <zephyr/kernel.h>
#include <zephyr/sys/sem.h>

namespace cler {

#ifndef CLER_ZEPHYR_STACK_SIZE
#define CLER_ZEPHYR_STACK_SIZE 4096
#endif

#ifndef CLER_ZEPHYR_PRIORITY
#define CLER_ZEPHYR_PRIORITY 5
#endif

struct ZephyrTaskPolicy : TaskPolicyBase<ZephyrTaskPolicy> {
    struct TaskData {
        void* callable;
        void (*invoke)(void*);
        void (*destroy)(void*);
        struct k_sem* completion_sem;
    };

    struct TaskWrapper {
        struct k_thread thread_data;
        k_thread_stack_t* stack_mem;
        struct k_sem completion_sem;
        TaskData* task_data;
        bool task_completed = false;
    };

    using task_type = TaskWrapper;

    template<typename Func>
    struct TaskHelper {
        static void invoke(void* callable) {
            (*static_cast<Func*>(callable))();
        }

        static void destroy(void* callable) {
            delete static_cast<Func*>(callable);
        }
    };

    template<typename Func>
    static task_type create_task(Func&& f) {
        task_type wrapper;

        wrapper.stack_mem = new k_thread_stack_t[CLER_ZEPHYR_STACK_SIZE];
        if (!wrapper.stack_mem) return wrapper;

        k_sem_init(&wrapper.completion_sem, 0, 1);

        using FuncType = typename std::decay<Func>::type;
        auto* task_data = new TaskData{
            new FuncType(std::forward<Func>(f)),
            &TaskHelper<FuncType>::invoke,
            &TaskHelper<FuncType>::destroy,
            &wrapper.completion_sem
        };

        wrapper.task_data = task_data;

        k_thread_create(&wrapper.thread_data,
                        wrapper.stack_mem,
                        CLER_ZEPHYR_STACK_SIZE,
                        thread_entry_point,
                        task_data, nullptr, nullptr,
                        CLER_ZEPHYR_PRIORITY,
                        0, K_NO_WAIT);

        return wrapper;
    }

    static void join_task(task_type& wrapper) {
        if (!wrapper.task_completed && wrapper.task_data) {
            k_sem_take(&wrapper.completion_sem, K_FOREVER);

            wrapper.task_data->destroy(wrapper.task_data->callable);
            delete wrapper.task_data;
            delete[] wrapper.stack_mem;
            wrapper.task_completed = true;
        }
    }

    static void yield() {
        k_yield();
    }

    static void sleep_us(size_t us) {
        k_usleep(us);
    }

private:
    static void thread_entry_point(void* p1, void*, void*) {
        auto* data = static_cast<TaskData*>(p1);
        if (data && data->callable && data->invoke) {
            data->invoke(data->callable);
            k_sem_give(data->completion_sem);
        }
    }
};

// Factory and alias for Zephyr

template<typename... Runners>
auto make_zephyr_flowgraph(Runners&&... runners) {
    return cler::FlowGraph<cler::ZephyrTaskPolicy, std::decay_t<Runners>...>(
        std::forward<Runners>(runners)...);
}

template<typename... BlockRunners>
using ZephyrFlowGraph = FlowGraph<ZephyrTaskPolicy, BlockRunners...>;

} // namespace cler