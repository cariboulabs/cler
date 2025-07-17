#pragma once

#include "cler.hpp"
#include <array>
#include <utility>

namespace cler {

// Custom allocator concept for embedded systems
template<typename T>
concept Allocator = requires(T& a, typename T::value_type* p, size_t n) {
    typename T::value_type;
    { a.allocate(n) } -> std::same_as<typename T::value_type*>;
    { a.deallocate(p, n) } -> std::same_as<void>;
};

// Static pool allocator for embedded systems
template<typename T, size_t N>
class StaticPoolAllocator {
public:
    using value_type = T;
    
    StaticPoolAllocator() = default;
    
    T* allocate(size_t n) {
        if (n > N || allocated) {
            throw std::bad_alloc();
        }
        allocated = true;
        return reinterpret_cast<T*>(storage.data());
    }
    
    void deallocate(T*, size_t) {
        allocated = false;
    }
    
private:
    alignas(T) std::array<char, N * sizeof(T)> storage;
    bool allocated = false;
};

// RTOS task wrapper for embedded systems (can be specialized for FreeRTOS, Zephyr, etc.)
template<typename Callable>
class EmbeddedTask {
public:
    using TaskFunc = void(*)(void*);
    
    explicit EmbeddedTask(Callable&& callable) 
        : callable_(std::forward<Callable>(callable)) {}
    
    void start() {
        // In real implementation, this would create an RTOS task
        // For now, we'll use a simple function pointer approach
        task_entry(this);
    }
    
    void stop() {
        should_stop_ = true;
    }
    
    bool should_stop() const { return should_stop_; }
    
private:
    static void task_entry(void* param) {
        auto* self = static_cast<EmbeddedTask*>(param);
        self->callable_();
    }
    
    Callable callable_;
    std::atomic<bool> should_stop_{false};
};

// Static FlowGraph for embedded systems - no dynamic allocation
template<typename... BlockRunners>
class StaticFlowGraph {
public:
    static constexpr size_t N = sizeof...(BlockRunners);
    
    explicit StaticFlowGraph(BlockRunners... runners)
        : runners_(std::forward<BlockRunners>(runners)...) {}
    
    // Non-copyable and non-movable
    StaticFlowGraph(const StaticFlowGraph&) = delete;
    StaticFlowGraph& operator=(const StaticFlowGraph&) = delete;
    StaticFlowGraph(StaticFlowGraph&&) = delete;
    StaticFlowGraph& operator=(StaticFlowGraph&&) = delete;
    
    template<typename TaskFactory>
    void run_with_tasks(TaskFactory& factory) {
        stop_flag_ = false;
        
        auto launch_tasks = [this, &factory]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((tasks_[Is] = factory.create_task([this, Is]() {
                this->run_block<Is>();
            })), ...);
            
            ((tasks_[Is]->start()), ...);
        };
        
        launch_tasks(std::make_index_sequence<N>{});
    }
    
    void stop() {
        stop_flag_ = true;
        for (auto& task : tasks_) {
            if (task) task->stop();
        }
    }
    
    bool is_stopped() const {
        return stop_flag_.load(std::memory_order_acquire);
    }
    
private:
    template<size_t I>
    void run_block() {
        auto& runner = std::get<I>(runners_);
        auto& stats = stats_[I];
        
        stats.name = runner.block->name();
        
        while (!stop_flag_) {
            Result<Empty, Error> result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            
            if (result.is_err()) {
                auto err = result.unwrap_err();
                
                if (err > Error::TERMINATE_FLOWGRAPH) {
                    stop_flag_ = true;
                    return;
                }
                
                if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                    stats.failed_procedures++;
                    // Simple yield for embedded systems
                    for (volatile int i = 0; i < 100; ++i);
                } else {
                    // Handle other errors
                    for (volatile int i = 0; i < 1000; ++i);
                }
            } else {
                stats.successful_procedures++;
            }
        }
    }
    
    std::tuple<BlockRunners...> runners_;
    std::array<void*, N> tasks_{};
    std::array<BlockExecutionStats, N> stats_{};
    std::atomic<bool> stop_flag_{false};
};

// Channel with custom allocator support
template<typename T, size_t N = 0, typename Alloc = std::allocator<T>>
class AllocatorChannel : public ChannelBase<T> {
    using Queue = typename std::conditional_t<
        N == 0,
        dro::SPSCQueue<T, 0, Alloc>,
        dro::SPSCQueue<T, N>
    >;
    
public:
    AllocatorChannel() requires (N > 0) : queue_() {}
    
    explicit AllocatorChannel(size_t size, const Alloc& alloc = Alloc{}) 
        requires (N == 0) : queue_(size, alloc) {}
    
    // Implement all virtual methods from ChannelBase
    size_t size() const override { return queue_.size(); }
    size_t space() const override { return queue_.space(); }
    void push(const T& v) override { queue_.push(v); }
    void pop(T& v) override { queue_.pop(v); }
    bool try_push(const T& v) override { return queue_.try_push(v); }
    bool try_pop(T& v) override { return queue_.try_pop(v); }
    size_t writeN(const T* data, size_t n) override { return queue_.writeN(data, n); }
    size_t readN(T* data, size_t n) override { return queue_.readN(data, n); }
    
    size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) override {
        return queue_.peek_write(ptr1, size1, ptr2, size2);
    }
    
    void commit_write(size_t count) override {
        queue_.commit_write(count);
    }
    
    size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) override {
        return queue_.peek_read(ptr1, size1, ptr2, size2);
    }
    
    void commit_read(size_t count) override {
        queue_.commit_read(count);
    }
    
private:
    Queue queue_;
};

// Helper to create static channels with compile-time sizes
template<typename T, size_t Size>
using StaticChannel = AllocatorChannel<T, Size>;

// Example RTOS task factory for FreeRTOS
class FreeRTOSTaskFactory {
public:
    template<typename Callable>
    EmbeddedTask<Callable>* create_task(Callable&& callable) {
        // In real implementation, this would use xTaskCreate
        static EmbeddedTask<Callable> task(std::forward<Callable>(callable));
        return &task;
    }
};

} // namespace cler