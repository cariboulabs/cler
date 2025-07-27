#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include "cler_embeddable_string.hpp"
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <chrono> // for timing measurements in FlowGraph
#include <tuple> // for storing block runners
#include <cassert> // for assertions
#include <atomic> // for atomic adaptive sleep state
#include <limits> // for std::numeric_limits

namespace cler {

    // Configurable at compile-time for different target platforms
    #ifndef CLER_DEFAULT_MAX_WORKERS
    #define CLER_DEFAULT_MAX_WORKERS (8)  // Conservative default for embedded systems
    #endif
    constexpr size_t DEFAULT_MAX_WORKERS = CLER_DEFAULT_MAX_WORKERS;

    enum class Error {
        // Non-fatal errors (< TERMINATE_FLOWGRAPH)
        NotEnoughSamples,
        NotEnoughSpace,
        ProcedureError,
        BadData,
        
        // Fatal errors (>= TERMINATE_FLOWGRAPH)
        TERMINATE_FLOWGRAPH,
        TERM_InvalidChannelIndex,
        TERM_ProcedureError,
        TERM_IOError,
        TERM_EOFReached,
    };
    
    // Helper function for error classification
    constexpr bool is_fatal(Error error) {
        return error >= Error::TERMINATE_FLOWGRAPH;
    }

    inline const char* to_str(Error error) {
        switch (error) {
            case Error::NotEnoughSpace: return "Not enough space in output buffers";
            case Error::NotEnoughSamples: return "Not enough samples in input buffers";
            case Error::ProcedureError: return "Procedure error";
            case Error::BadData: return "Bad data received";
            case Error::TERM_InvalidChannelIndex: return "TERM: Invalid channel index";
            case Error::TERM_ProcedureError: return "TERM: Procedure error";
            case Error::TERM_IOError: return "TERM: IO error";
            case Error::TERM_EOFReached: return "TERM: EOF reached";
            default: return "Unknown error";
        }
    }

    template <typename T>
    struct ChannelBase {
        virtual ~ChannelBase() = default;
        virtual size_t size() const = 0;
        virtual size_t space() const = 0;
        virtual void push(const T&) = 0;
        virtual void pop(T&) = 0;
        virtual bool try_push(const T&) = 0;
        virtual bool try_pop(T&) = 0;
        virtual size_t writeN(const T* data, size_t n) = 0;
        virtual size_t readN(T* data, size_t n) = 0;
        virtual size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) = 0;
        virtual void commit_write(size_t count) = 0;
        virtual size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) = 0;
        virtual void commit_read(size_t count) = 0;
    };

    template <typename T, size_t N = 0>
    struct Channel : public ChannelBase<T> {
        dro::SPSCQueue<T, N> _queue;
        Channel() = default;

        template<size_t M = N, typename = std::enable_if_t<M == 0>>
        Channel(size_t size) : _queue(size) {
            if (size == 0) throw std::invalid_argument("Channel size must be greater than zero.");
        }

        size_t size() const override { return _queue.size(); }
        size_t space() const override { return _queue.space(); }
        void push(const T& v) override { _queue.push(v); }
        void pop(T& v) override { _queue.pop(v); }
        bool try_push(const T& v) override { return _queue.try_push(v); }
        bool try_pop(T& v) override { return _queue.try_pop(v); }
        size_t writeN(const T* data, size_t n) override { return _queue.writeN(data, n); }
        size_t readN(T* data, size_t n) override { return _queue.readN(data, n); }
        size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) override {
            return _queue.peek_write(ptr1, size1, ptr2, size2);
        }
        void commit_write(size_t count) override { _queue.commit_write(count); }
        size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) override {
            return _queue.peek_read(ptr1, size1, ptr2, size2);
        }
        void commit_read(size_t count) override { _queue.commit_read(count); }
    };

    struct BlockBase {
        explicit BlockBase(const char* name) : _name(name) {}
        explicit BlockBase(const EmbeddableString<64>& name) : _name(name) {}
        const char* name() const { return _name.c_str(); }
        BlockBase(const BlockBase&) = delete;
        BlockBase& operator=(const BlockBase&) = delete;
        BlockBase(BlockBase&&) = delete;
        BlockBase& operator=(BlockBase&&) = delete;
    private:
        EmbeddableString<64> _name;
    };

    template<typename T>
    struct channel_to_base { using type = T; };
    template<typename T, size_t N>
    struct channel_to_base<Channel<T, N>> { using type = ChannelBase<T>; };
    template<typename T>
    using channel_to_base_t = typename channel_to_base<T>::type;

    template<typename Block, typename... Channels>
    struct BlockRunner {
        Block* block;
        std::tuple<Channels*...> outputs;

        template<typename... InputChannels>
        BlockRunner(Block* blk, InputChannels*... outs)
            : block(blk), outputs(static_cast<Channels*>(outs)...) {}
    };

    // C++17 deduction guide: automatically deduces channel base types from concrete channel types
    // This allows: BlockRunner(&block, &channel) instead of BlockRunner<Block, ChannelBase<T>>(&block, &channel)
    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, channel_to_base_t<Channels>...>;

    struct BlockExecutionStats {
        EmbeddableString<64> name;
        size_t successful_procedures = 0;
        size_t failed_procedures = 0;
        double total_dead_time_s = 0.0;
        double total_runtime_s = 0.0;
        double final_adaptive_sleep_us = 0.0;
        std::atomic<double> current_adaptive_sleep_us{0.0};
        std::atomic<size_t> consecutive_fails{0};
        double get_avg_execution_time_us() const {
            return successful_procedures > 0 ? (total_runtime_s * 1e6) / successful_procedures : 0.0;
        }
        
        double get_cpu_utilization_percent() const {
            return total_runtime_s > 0 ? ((total_runtime_s - total_dead_time_s) / total_runtime_s) * 100.0 : 0.0;
        }
        
        double get_avg_dead_time_per_fail() const {
            return failed_procedures > 0 ? total_dead_time_s / failed_procedures : 0.0;
        }
    };

    // Enhanced scheduling types for performance optimization  
    enum class SchedulerType {
        ThreadPerBlock,        //Best for small flowgraphs or debugging
        FixedThreadPool,       //Best for uniform workloads
        WorkStealing          //Best for unpredictable workloads with simple lock-free design
    };
    
    // Configuration for performance optimization
    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 4;  // Number of worker threads (used by FixedThreadPool and WorkStealing, ignored for ThreadPerBlock)

        // Optimizes CPU usage, usually at the cost of reducing throughput
        // Most useful for:
        // - Intermittent sensor data  
        // - Network packet processing with gaps
        // - File processing with I/O delays
        bool adaptive_sleep = false;
        double adaptive_sleep_multiplier = 1.5;  // How aggressively to increase sleep time
        double adaptive_sleep_max_us = 5000.0;          // Maximum sleep time in microseconds
        size_t adaptive_sleep_fail_threshold = 10;  // Start sleeping after N consecutive fails

        // Load balancing parameters (only used with WorkStealing scheduler)
        size_t load_balancing_interval = 1000;  // Rebalance every N procedure calls
        double load_balancing_threshold = 0.2;  // 20% imbalance triggers rebalancing
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        static constexpr std::size_t MaxBlocks = sizeof...(BlockRunners);  // Clean compile-time constant
        static_assert(_N > 0, "FlowGraph must have at least one block");
        static_assert(_N <= 256, "FlowGraph cannot have more than 256 blocks (due to uint8_t indexing)");
        using OnErrTerminateCallback = void (*)(void* context);

        FlowGraph(BlockRunners... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(std::move(runners))...)) {}

        ~FlowGraph() { stop(); }

        FlowGraph(const FlowGraph&) = delete;
        FlowGraph(FlowGraph&&) = delete;
        FlowGraph& operator=(const FlowGraph&) = delete;
        FlowGraph& operator=(FlowGraph&&) = delete;

        void set_on_err_terminate_cb(OnErrTerminateCallback cb, void* context) {
            _on_err_terminate_cb = cb;
            _on_err_terminate_context = context;
        }

        OnErrTerminateCallback on_err_terminate_cb() const { return _on_err_terminate_cb; }
        void* on_err_terminate_context() const { return _on_err_terminate_context; }

        void run(const FlowGraphConfig& config = FlowGraphConfig{}) {
            _config = config;
            _stop_flag.store(false, std::memory_order_release);
            
            // Runtime assertion: WorkStealing scheduler is incompatible with adaptive sleep
            if (config.scheduler == SchedulerType::WorkStealing && config.adaptive_sleep) {
                assert(false && "WorkStealing scheduler does not support adaptive_sleep. Use ThreadPerBlock or FixedThreadPool instead.");
            }
            
            switch (config.scheduler) {
                case SchedulerType::ThreadPerBlock:
                    run_thread_per_block(config);
                    break;
                    
                case SchedulerType::FixedThreadPool:
                    run_with_cache_optimized_scheduler(config);
                    break;
                    
                case SchedulerType::WorkStealing:
                    run_with_work_stealing(config);
                    break;
            }
        }
        
        template<typename Rep, typename Period>
        void run_for(const std::chrono::duration<Rep, Period>& duration, const FlowGraphConfig& config = FlowGraphConfig{}) {
            // Start the flowgraph
            auto start_time = std::chrono::high_resolution_clock::now();
            run(config);
            
            // For longer durations, use sleep_us to avoid busy waiting
            static constexpr int64_t PRECISE_TIMING_THRESHOLD_US = 100000;  // 100ms
            static constexpr int64_t PRECISE_TIMING_BUFFER_US = 50000;      // 50ms
            
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            if (total_us > PRECISE_TIMING_THRESHOLD_US) { // More than 100ms
                // Sleep for most of the duration, leaving 50ms for precise timing
                TaskPolicy::sleep_us(total_us - PRECISE_TIMING_BUFFER_US);
            }
            
            // Use yield for the remaining time for precise timing
            while (std::chrono::high_resolution_clock::now() - start_time < duration) {
                TaskPolicy::yield();
            }
            
            // Stop the flowgraph
            stop();
        }

        void stop() {
            _stop_flag.store(true, std::memory_order_release);
            // Only join tasks that were actually created to prevent hang
            for (size_t i = 0; i < _active_task_count; ++i) {
                TaskPolicy::join_task(_tasks[i]);
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_acquire);
        }

        // Immutable config accessor
        const FlowGraphConfig& config() const { return _config; }
        const std::array<BlockExecutionStats, _N>& stats() const { return _stats; }

    private:
        // Block-centric adaptive sleep logic (works with all schedulers)
        void handle_adaptive_sleep(size_t block_idx, bool procedure_succeeded) {
            if (!_config.adaptive_sleep) return;
            
            auto& stats = _stats[block_idx];
            
            if (procedure_succeeded) {
                // Exponential decay instead of hard reset for better bursty workload handling
                stats.consecutive_fails.store(0);
                double current_sleep = stats.current_adaptive_sleep_us.load();
                stats.current_adaptive_sleep_us.store(current_sleep * 0.5);  // Gradual decay
            } else {
                // Increment failure count
                size_t fails = stats.consecutive_fails.fetch_add(1) + 1;
                
                // Check if we should sleep
                if (fails > _config.adaptive_sleep_fail_threshold) {
                    double current_sleep = stats.current_adaptive_sleep_us.load();
                    
                    if (current_sleep == 0.0) {
                        // Start sleeping with 1 microsecond
                        static constexpr double INITIAL_SLEEP_US = 1.0;
                        stats.current_adaptive_sleep_us.store(INITIAL_SLEEP_US);
                        TaskPolicy::sleep_us(static_cast<size_t>(INITIAL_SLEEP_US));
                    } else {
                        // Exponential backoff with deterministic jitter to prevent thundering herd
                        double base_sleep = current_sleep * _config.adaptive_sleep_multiplier;
                        
                        // Deterministic jitter based on block index (10% variation)
                        static constexpr double JITTER_FACTOR = 0.1;
                        double block_jitter = 1.0 + JITTER_FACTOR * (double(block_idx % 10) / 10.0 - 0.5);
                        
                        double new_sleep = std::min(
                            base_sleep * block_jitter,
                            _config.adaptive_sleep_max_us
                        );
                        stats.current_adaptive_sleep_us.store(new_sleep);
                        TaskPolicy::sleep_us(static_cast<size_t>(new_sleep));
                    }
                } else {
                    // Not enough failures yet, just yield
                    TaskPolicy::yield();
                }
            }
        }
        
        // C++17 compatible member template functions replacing templated lambdas
        template<std::size_t I>
        void run_block_at_index_thread_per_block(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];

            stats.name = runner.block->name();

            auto t_start = std::chrono::high_resolution_clock::now();
            auto t_last = t_start;

            size_t successful = 0;
            size_t failed = 0;
            double total_dead_time_s = 0.0;

            while (!_stop_flag) {
                auto t_now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> dt = t_now - t_last;
                t_last = t_now;

                Result<Empty, Error> result = std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);

                if (result.is_err()) {
                    failed++;
                    auto err = result.unwrap_err();

                    if (is_fatal(err)) {
                        _stop_flag.store(true, std::memory_order_release);
                        if (_on_err_terminate_cb) {
                            _on_err_terminate_cb(_on_err_terminate_context);
                        }
                        return;
                    }

                    if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                        total_dead_time_s += dt.count();
                        handle_adaptive_sleep(I, false);
                    } else {
                        TaskPolicy::yield();
                    }

                } else {
                    successful++;
                    handle_adaptive_sleep(I, true);
                }
            }

            auto t_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> total_runtime_s = t_end - t_start;

            stats.successful_procedures = successful;
            stats.failed_procedures = failed;
            stats.total_dead_time_s = total_dead_time_s;
            stats.final_adaptive_sleep_us = config.adaptive_sleep ? stats.current_adaptive_sleep_us.load() : 0.0;
            stats.total_runtime_s = total_runtime_s.count();
        }
        
        template<std::size_t... Is>
        void launch_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config) {
            // C++17 fold expression: validates all indices are within bounds at compile time
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            ((_tasks[Is] = TaskPolicy::create_task([this, config]() {
                run_block_at_index_thread_per_block<Is>(config);
            })), ...);
            _active_task_count = _N;  // ThreadPerBlock creates one task per block
        }
        
        template<std::size_t... Is>
        void init_stats_impl(std::index_sequence<Is...>) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            ((_stats[Is].name = std::get<Is>(_runners).block->name()), ...);
        }
        
        template<std::size_t... Is>
        bool execute_block_dispatch_impl(std::index_sequence<Is...>, size_t index, const FlowGraphConfig& config) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            bool result = false;
            ((index == Is ? (result = execute_block_at_index_helper<Is>(config), true) : false) || ...);
            return result;
        }
        
        template<std::size_t... Is>
        bool execute_block_without_sleep_dispatch_impl(std::index_sequence<Is...>, size_t index) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            bool result = false;
            ((index == Is ? (result = execute_block_without_sleep_helper<Is>(), true) : false) || ...);
            return result;
        }
        
        void run_thread_per_block(const FlowGraphConfig& config) {
            // Initialize stats for all blocks
            initialize_block_stats();
            // Launch one thread per block using C++17 compatible approach
            launch_tasks_impl(std::make_index_sequence<_N>{}, config);
        }

        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag{false};
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        OnErrTerminateCallback _on_err_terminate_cb = nullptr;
        void* _on_err_terminate_context = nullptr;
        std::array<std::chrono::high_resolution_clock::time_point, _N> _block_start_times;
        size_t _active_task_count{0};  // Track actual created tasks to fix stop() hang
        std::array<std::atomic<size_t>, DEFAULT_MAX_WORKERS> _worker_iterations{};
        
        // Initialize block stats with names
        void initialize_block_stats() {
            init_stats_impl(std::make_index_sequence<_N>{});
        }
        
        
        // Cache-Optimized Scheduler - Ultra-high throughput with minimal cache misses
        template<size_t MaxBlocksParam, size_t MaxWorkers = DEFAULT_MAX_WORKERS>
        class CacheOptimizedScheduler {
            using block_index_t = uint8_t;
            
            // Compile-time validation
            static_assert(MaxBlocksParam >= 1, "Must support at least one block");
            static_assert(MaxWorkers >= 1, "Must support at least one worker");
            static_assert(MaxBlocksParam <= std::numeric_limits<block_index_t>::max(), 
                          "MaxBlocksParam exceeds block_index_t capacity");
            static_assert(MaxWorkers <= 32, "MaxWorkers should be reasonable for embedded systems");
            
            // Align to cache line to prevent false sharing
            struct alignas(64) WorkerQueue {
                std::array<block_index_t, MaxBlocksParam> blocks;
                uint32_t count = 0;
                uint32_t current = 0;
                
                bool get_block(size_t& block_idx_out) {
                    if (current < count) {
                        block_idx_out = blocks[current++];
                        return true;
                    }
                    return false;
                }
                
                void reset() {
                    current = 0;
                }
            };
            
            std::array<WorkerQueue, MaxWorkers> queues;
            size_t num_blocks = 0;
            size_t num_workers = 0;
            
        public:
            void initialize(size_t blocks, size_t workers) {
                num_blocks = blocks;
                num_workers = workers;
                
                // Pre-distribute all blocks round-robin across workers
                for (size_t i = 0; i < blocks; ++i) {
                    size_t worker = i % workers;
                    queues[worker].blocks[queues[worker].count++] = static_cast<block_index_t>(i);
                }
            }
            
            bool get_next_block(size_t worker_id, size_t& block_idx_out) {
                // Super fast path - no atomics!
                if (queues[worker_id].get_block(block_idx_out)) {
                    return true;
                }
                
                // Reset and go again (continuous round-robin)
                queues[worker_id].reset();
                return queues[worker_id].get_block(block_idx_out);
            }
        };
        
        // Work-Stealing Scheduler - Simple and lock-free
        template<size_t MaxBlocksParam, size_t MaxWorkers = DEFAULT_MAX_WORKERS>
        class WorkStealingScheduler {
            // Type alias for block indices
            using block_index_t = uint8_t;
            
            // Compile-time validation
            static_assert(MaxBlocksParam >= 1, "Must support at least one block");
            static_assert(MaxWorkers >= 1, "Must support at least one worker");
            static_assert(MaxBlocksParam <= std::numeric_limits<block_index_t>::max(), 
                          "MaxBlocksParam exceeds block_index_t capacity");
            static_assert(MaxWorkers <= 32, "MaxWorkers should be reasonable for embedded systems");
            
            // Each worker has a local queue of blocks to process
            struct alignas(64) WorkerQueue {  // Cache line alignment to prevent false sharing
                std::array<block_index_t, MaxBlocksParam> blocks;
                std::atomic<uint32_t> head{0};
                std::atomic<uint32_t> tail{0};
                
                bool try_push(block_index_t block_idx) {
                    uint32_t t = tail.load(std::memory_order_relaxed);
                    uint32_t h = head.load(std::memory_order_acquire);
                    
                    // Check if queue is full
                    if (t - h >= MaxBlocksParam) return false;
                    
                    blocks[t % MaxBlocksParam] = block_idx;
                    tail.store(t + 1, std::memory_order_release);
                    return true;
                }
                
                bool try_pop(block_index_t& block_idx) {
                    uint32_t h = head.load(std::memory_order_relaxed);
                    uint32_t t = tail.load(std::memory_order_acquire);
                    
                    if (h >= t) return false;
                    
                    block_idx = blocks[h % MaxBlocksParam];
                    head.store(h + 1, std::memory_order_release);
                    return true;
                }
                
                bool try_steal(block_index_t& block_idx) {
                    // Steal from head (opposite end from owner to minimize contention)
                    uint32_t h = head.load(std::memory_order_acquire);
                    uint32_t t = tail.load(std::memory_order_acquire);
                    
                    // Loop to handle CAS failures
                    while (h < t) {
                        // Read the block we want to steal
                        block_idx = blocks[h % MaxBlocksParam];
                        
                        // Try to advance head atomically
                        if (head.compare_exchange_weak(h, h + 1,
                                                      std::memory_order_release,
                                                      std::memory_order_acquire)) {
                            return true;  // Successfully stole the block
                        }
                        // If CAS failed, h was updated, loop will re-check condition
                    }
                    return false;
                }
                
                size_t size() const {
                    uint32_t h = head.load(std::memory_order_relaxed);
                    uint32_t t = tail.load(std::memory_order_relaxed);
                    return (h <= t) ? (t - h) : 0;
                }
            };
            
        private:
            std::array<WorkerQueue, MaxWorkers> queues;
            size_t num_workers = 0;
            size_t num_blocks = 0;
            
            // Track which blocks are being processed (for work returning)
            std::array<std::atomic<int8_t>, MaxBlocksParam> block_owner;  // -1 = unassigned, >=0 = worker id
            
        public:
            void initialize(size_t blocks, size_t workers) {
                // Runtime validation
                if (blocks == 0 || blocks > MaxBlocksParam) {
                    assert(false && "Invalid block count");
                    return;
                }
                if (workers == 0 || workers > MaxWorkers) {
                    assert(false && "Invalid worker count");
                    return;
                }
                
                num_blocks = blocks;
                num_workers = workers;
                
                // Initialize block ownership
                for (size_t i = 0; i < blocks; ++i) {
                    block_owner[i].store(-1, std::memory_order_relaxed);
                }
                
                // Initial distribution: round-robin
                for (size_t i = 0; i < blocks; ++i) {
                    size_t worker_id = i % workers;
                    queues[worker_id].try_push(static_cast<block_index_t>(i));
                }
            }
            
            bool get_next_block(size_t worker_id, size_t& block_idx_out) {
                if (worker_id >= num_workers) return false;
                
                block_index_t local_block_idx;
                
                // Try local queue first (fast path)
                if (queues[worker_id].try_pop(local_block_idx)) {
                    block_idx_out = local_block_idx;
                    block_owner[local_block_idx].store(static_cast<int8_t>(worker_id), 
                                                       std::memory_order_relaxed);
                    return true;
                }
                
                // Try stealing from others (start from next worker for fairness)
                for (size_t i = 1; i < num_workers; ++i) {
                    size_t victim = (worker_id + i) % num_workers;
                    if (queues[victim].try_steal(local_block_idx)) {
                        block_idx_out = local_block_idx;
                        block_owner[local_block_idx].store(static_cast<int8_t>(worker_id), 
                                                           std::memory_order_relaxed);
                        return true;
                    }
                }
                
                return false;
            }
            
            // Return a block to the work pool (useful if a block fails repeatedly)
            void return_block(size_t worker_id, size_t block_idx) {
                if (worker_id >= num_workers || block_idx >= num_blocks) return;
                
                // Clear ownership
                block_owner[block_idx].store(-1, std::memory_order_relaxed);
                
                // Try to put it back in the worker's queue
                if (!queues[worker_id].try_push(static_cast<block_index_t>(block_idx))) {
                    // If local queue is full, try other workers
                    for (size_t i = 1; i <= num_workers; ++i) {
                        size_t target = (worker_id + i) % num_workers;
                        if (queues[target].try_push(static_cast<block_index_t>(block_idx))) {
                            break;
                        }
                    }
                }
            }
            
            // Get queue sizes for monitoring
            void get_queue_sizes(std::array<size_t, MaxWorkers>& sizes) const {
                for (size_t i = 0; i < num_workers; ++i) {
                    sizes[i] = queues[i].size();
                }
            }
        };
        
        WorkStealingScheduler<MaxBlocks, DEFAULT_MAX_WORKERS> work_stealing_scheduler;
        CacheOptimizedScheduler<MaxBlocks, DEFAULT_MAX_WORKERS> cache_optimized_scheduler;
        
        // Enhanced scheduling implementations
        
        bool execute_block_at_index_with_metrics(size_t index, const FlowGraphConfig& config) {
            // Similar to execute_block_at_index but with metrics tracking using C++17 compatible approach
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        // Cache-optimized FixedThreadPool implementation
        void run_with_cache_optimized_scheduler(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            
            // Validate worker count - must be at least 2 for cache-optimized scheduling
            size_t num_workers = config.num_workers;
            assert(num_workers >= 2 && "CacheOptimizedScheduler requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");
            
            // Initialize stats for all blocks
            initialize_block_stats();
            
            // For cache-optimized scheduler: handle different worker/block ratios
            if (num_workers >= _N) {
                // More workers than blocks - use thread-per-block (current behavior)
                run_thread_per_block(config);
            } else {
                // Fewer workers than blocks - use cache-optimized scheduler
                // Initialize cache-optimized scheduler with round-robin block distribution
                cache_optimized_scheduler.initialize(_N, num_workers);
                
                // Record start time for all blocks
                auto start_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    _block_start_times[i] = start_time;
                }
                
                // Create worker tasks using cache-optimized scheduler
                _active_task_count = 0;
                for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                    _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, config]() {
                        run_cache_optimized_worker(worker_id, config);
                    });
                    _active_task_count++;
                }
            }
        }
        
        void run_cache_optimized_worker(size_t worker_id, const FlowGraphConfig& config) {
            while (!_stop_flag) {
                bool did_work = false;
                size_t block_idx;
                
                // Get next block from cache-optimized scheduler (super fast - no atomics!)
                while (cache_optimized_scheduler.get_next_block(worker_id, block_idx)) {
                    if (_stop_flag) break;
                    
                    // Track timing for dead time calculation
                    auto t_before = std::chrono::high_resolution_clock::now();
                    bool block_did_work = execute_block_at_index(block_idx, config);
                    auto t_after = std::chrono::high_resolution_clock::now();
                    
                    // Update dead time if block failed to process
                    if (!block_did_work) {
                        std::chrono::duration<double> dt = t_after - t_before;
                        _stats[block_idx].total_dead_time_s += dt.count();
                    }
                    
                    did_work = did_work || block_did_work;
                }
                
                if (!did_work) {
                    // No work available, yield to other workers
                    TaskPolicy::yield();
                }
            }
            
            // Finalize stats when worker exits
            auto end_time = std::chrono::high_resolution_clock::now();
            // Update stats for all blocks this worker processed
            // Note: Cache-optimized scheduler handles block assignment, so we update all blocks
            // The scheduler ensures proper round-robin distribution
            for (size_t i = 0; i < _N; ++i) {
                std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                _stats[i].total_runtime_s = total_runtime.count();
                _stats[i].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[i].current_adaptive_sleep_us.load() : 0.0;
            }
        }
        
        
        template<size_t I>
        bool execute_block_at_index_helper(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");
            if constexpr (I >= _N) {
                return false;  // Invalid index (redundant but kept for runtime safety)
            }
            
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];
            
            // Execute procedure and handle errors
            auto result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            
            if (result.is_err()) {
                stats.failed_procedures++;
                auto err = result.unwrap_err();
                if (is_fatal(err)) {
                    _stop_flag.store(true, std::memory_order_release);
                    if (_on_err_terminate_cb) {
                        _on_err_terminate_cb(_on_err_terminate_context);
                    }
                }
                
                // Handle adaptive sleep for failed procedure
                if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                    handle_adaptive_sleep(I, false);
                } else {
                    TaskPolicy::yield();
                }
                
                return false;
            } else {
                stats.successful_procedures++;
                
                // Handle adaptive sleep for successful procedure
                handle_adaptive_sleep(I, true);
                
                return true;
            }
        }
        
        bool execute_block_at_index(size_t index, const FlowGraphConfig& config) {
            // Runtime dispatch to compile-time template using C++17 compatible approach
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        
        template<size_t I>
        bool execute_block_without_sleep_helper() {
            static_assert(I < _N, "Block index out of bounds");
            if constexpr (I >= _N) {
                return false;  // Invalid index (redundant but kept for runtime safety)
            }
            
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];
            
            // Execute procedure and handle errors
            auto result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            
            if (result.is_err()) {
                stats.failed_procedures++;
                auto err = result.unwrap_err();
                if (is_fatal(err)) {
                    _stop_flag.store(true, std::memory_order_release);
                    if (_on_err_terminate_cb) {
                        _on_err_terminate_cb(_on_err_terminate_context);
                    }
                }
                
                // No adaptive sleep - just yield
                TaskPolicy::yield();
                return false;
            } else {
                stats.successful_procedures++;
                // No adaptive sleep - just return success
                return true;
            }
        }
        
        bool execute_block_without_sleep(size_t index) {
            // Runtime dispatch to compile-time template for non-adaptive sleep execution
            return execute_block_without_sleep_dispatch_impl(std::make_index_sequence<_N>{}, index);
        }
        
        // Work-stealing scheduler implementation
        void run_with_work_stealing(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            
            // Validate worker count - must be at least 2 for work stealing
            size_t num_workers = config.num_workers;
            assert(num_workers >= 2 && "WorkStealing requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");
            
            // Initialize stats for all blocks
            initialize_block_stats();
            
            // Initialize work-stealing scheduler
            work_stealing_scheduler.initialize(_N, num_workers);
            
            // Record start time for all blocks
            auto start_time = std::chrono::high_resolution_clock::now();
            for (size_t i = 0; i < _N; ++i) {
                _block_start_times[i] = start_time;
            }
            
            // Create worker tasks that use work stealing
            _active_task_count = 0;
            for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, config]() {
                    run_work_stealing_worker(worker_id, config);
                });
                _active_task_count++;
            }
        }
        
        void run_work_stealing_worker(size_t worker_id, const FlowGraphConfig& config) {
            size_t consecutive_steal_failures = 0;
            static constexpr size_t MAX_CONSECUTIVE_STEAL_FAILURES = 10;
            size_t round_robin_counter = 0;
            static constexpr size_t ROUND_ROBIN_INTERVAL = 100;  // Force RR every N attempts
            
            while (!_stop_flag) {
                size_t block_idx;
                bool got_work = false;
                
                // Periodic round-robin to prevent starvation
                if (++round_robin_counter >= ROUND_ROBIN_INTERVAL) {
                    round_robin_counter = 0;
                    // Each worker checks specific blocks in round-robin fashion
                    for (size_t i = worker_id; i < _N; i += config.num_workers) {
                        if (_stop_flag) break;
                        
                        // Force execution of this block to prevent starvation
                        auto start_time = std::chrono::high_resolution_clock::now();
                        bool block_did_work = execute_block_without_sleep(i);
                        auto end_time = std::chrono::high_resolution_clock::now();
                        
                        if (!block_did_work) {
                            std::chrono::duration<double> dt = end_time - start_time;
                            _stats[i].total_dead_time_s += dt.count();
                        }
                        
                        if (block_did_work) {
                            got_work = true;
                        }
                    }
                    
                    if (got_work) {
                        consecutive_steal_failures = 0;
                        continue;
                    }
                }
                
                // Normal work-stealing path
                if (work_stealing_scheduler.get_next_block(worker_id, block_idx)) {
                    consecutive_steal_failures = 0;  // Reset failure counter
                    
                    // Bounds checking
                    if (block_idx >= _N) {
                        assert(false && "Work-stealing scheduler returned invalid block index");
                        continue;
                    }
                    
                    // Track timing for statistics
                    auto start_time = std::chrono::high_resolution_clock::now();
                    
                    bool block_did_work = execute_block_without_sleep(block_idx);
                    
                    auto end_time = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> dt = end_time - start_time;
                    
                    if (!block_did_work) {
                        // Block had no work - update dead time
                        _stats[block_idx].total_dead_time_s += dt.count();
                        
                        // Consider returning the block if it repeatedly has no work
                        _stats[block_idx].consecutive_fails.fetch_add(1);
                        if (_stats[block_idx].consecutive_fails.load() > config.adaptive_sleep_fail_threshold * 2) {
                            // Return block to pool for potential redistribution
                            work_stealing_scheduler.return_block(worker_id, block_idx);
                            _stats[block_idx].consecutive_fails.store(0);
                        }
                    }
                } else {
                    // No work available from any queue
                    consecutive_steal_failures++;
                    
                    if (consecutive_steal_failures > MAX_CONSECUTIVE_STEAL_FAILURES) {
                        // All queues seem empty, sleep a bit longer
                        if (config.adaptive_sleep) {
                            TaskPolicy::sleep_us(100);  // 100 microseconds
                        } else {
                            TaskPolicy::yield();
                        }
                        consecutive_steal_failures = MAX_CONSECUTIVE_STEAL_FAILURES;  // Cap it
                    } else {
                        // Just yield for now
                        TaskPolicy::yield();
                    }
                }
            }
            
            // Finalize stats when worker exits
            auto end_time = std::chrono::high_resolution_clock::now();
            
            // We can't easily track which blocks this worker processed in work-stealing
            // So we'll update all block stats at the end based on their final runtime
            if (worker_id == 0) {  // Only do this once (first worker to exit)
                for (size_t i = 0; i < _N; ++i) {
                    std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                    _stats[i].total_runtime_s = total_runtime.count();
                    _stats[i].final_adaptive_sleep_us = config.adaptive_sleep ? 
                        _stats[i].current_adaptive_sleep_us.load() : 0.0;
                }
            }
        }
    };

    constexpr size_t DEFAULT_BUFFER_SIZE = 256;
    constexpr float PI = 3.14159265358979323846f;

} // namespace cler