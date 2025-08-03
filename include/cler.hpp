#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include "cler_embeddable_string.hpp"
#include "cler_platform.hpp"
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <chrono> // for timing measurements in FlowGraph
#include <tuple> // for storing block runners
#include <cassert> // for assertions
#include <atomic> // for atomic adaptive sleep state
#include <limits> // for std::numeric_limits

namespace cler {

    //here so we can insure blocks use this feature
    constexpr size_t DOUBLY_MAPPED_MIN_SIZE = dro::details::DOUBLY_MAPPED_MIN_SIZE;

    // Configurable at compile-time for different target platforms
    #ifndef CLER_DEFAULT_MAX_WORKERS
    #define CLER_DEFAULT_MAX_WORKERS (8)  // Conservative default for embedded systems
    #endif
    constexpr size_t DEFAULT_MAX_WORKERS = CLER_DEFAULT_MAX_WORKERS;

    enum class Error {
        // Non-fatal errors (< TERMINATE_FLOWGRAPH)
        Unknown,
        NotEnoughSamples,
        NotEnoughSpace,
        NotEnoughSpaceOrSamples, // for lazyness
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
            case Error::Unknown: return "Unknown error";
            case Error::NotEnoughSpace: return "Not enough space in output buffers";
            case Error::NotEnoughSamples: return "Not enough samples in input buffers";
            case Error::NotEnoughSpaceOrSamples: return "Not enough space or samples in buffers";
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
        virtual size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) = 0;
        virtual void commit_read(size_t count) = 0;
        virtual void commit_write(size_t count) = 0;
        virtual std::pair<const T*, std::size_t> read_dbf() = 0;
        virtual std::pair<T*, std::size_t> write_dbf() = 0;
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
        size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) override {
            return _queue.peek_read(ptr1, size1, ptr2, size2);
        }
        void commit_read(size_t count) override { _queue.commit_read(count); }
        void commit_write(size_t count) override { _queue.commit_write(count); }

        std::pair<const T*, std::size_t> read_dbf() override { return _queue.read_dbf(); }
        std::pair<T*, std::size_t> write_dbf() override { return _queue.write_dbf(); }
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

    // Scheduling types for performance optimization  
    enum class SchedulerType {
        ThreadPerBlock,        // Default: Simple, Debuggable
        FixedThreadPool        // Better for constrained systems
    };
    
    // Configuration for performance optimization
    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 4;  // Used by FixedThreadPool; ThreadPerBlock creates one thread per block

        // Optimizes CPU usage, usually at the cost of reducing throughput
        // Most useful for:
        // - Intermittent sensor data  
        // - Network packet processing with gaps
        // - File processing with I/O delays
        bool adaptive_sleep = false;
        double adaptive_sleep_multiplier = 1.5;  // How aggressively to increase sleep time
        double adaptive_sleep_max_us = 5000.0;          // Maximum sleep time in microseconds
        size_t adaptive_sleep_fail_threshold = 10;  // Start sleeping after N consecutive fails

        // Performance optimization: disable detailed stats collection for ultra-high throughput
        // When false: saves ~200 bytes per block, eliminates procedure counting and timing
        // When true: full diagnostics available (successful_procedures, timing, etc.)
        bool collect_detailed_stats = false;

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
            
            
            switch (config.scheduler) {
                case SchedulerType::ThreadPerBlock:
                    run_thread_per_block(config);
                    break;
                    
                case SchedulerType::FixedThreadPool:
                    run_fixed_thread_pool(config);
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
        
    public:
        // These methods must be public because they are called from lambdas passed to 
        // TaskPolicy::create_task(). Even though the lambdas are created within the class,
        // they are technically separate callable objects. Different compilers interpret 
        // lambda access to private members differently - GCC allows it while Clang doesn't.
        // Making these public ensures portability across compilers.
        
        // C++17 compatible member template functions replacing templated lambdas
        template<std::size_t I>
        void run_block_at_index_thread_per_block(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];

            if (config.collect_detailed_stats) {
                stats.name = runner.block->name();
            }

            // Only declare timing variables if needed
            std::chrono::high_resolution_clock::time_point t_start, t_last;
            size_t successful = 0, failed = 0;
            double total_dead_time_s = 0.0;

            if (config.collect_detailed_stats) {
                t_start = t_last = std::chrono::high_resolution_clock::now();
            }

            while (!_stop_flag) {
                std::chrono::duration<double> dt{};
                if (config.collect_detailed_stats) {
                    auto t_now = std::chrono::high_resolution_clock::now();
                    dt = t_now - t_last;
                    t_last = t_now;
                }

                Result<Empty, Error> result = std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);

                if (result.is_err()) {
                    if (config.collect_detailed_stats) {
                        failed++;
                    }
                    auto err = result.unwrap_err();

                    if (is_fatal(err)) {
                        _stop_flag.store(true, std::memory_order_release);
                        if (_on_err_terminate_cb) {
                            _on_err_terminate_cb(_on_err_terminate_context);
                        }
                        return;
                    }

                    if (err == Error::NotEnoughSpaceOrSamples || err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                        if (config.collect_detailed_stats) {
                            total_dead_time_s += dt.count();
                        }
                        handle_adaptive_sleep(I, false);
                    } else {
                        TaskPolicy::yield();
                    }

                } else {
                    if (config.collect_detailed_stats) {
                        successful++;
                    }
                    handle_adaptive_sleep(I, true);
                }
            }

            if (config.collect_detailed_stats) {
                auto t_end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> total_runtime_s = t_end - t_start;

                stats.successful_procedures = successful;
                stats.failed_procedures = failed;
                stats.total_dead_time_s = total_dead_time_s;
                stats.final_adaptive_sleep_us = config.adaptive_sleep ? stats.current_adaptive_sleep_us.load() : 0.0;
                stats.total_runtime_s = total_runtime_s.count();
            }
        }
        
    private:  // Return to private section for internal implementation
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
            (void)((index == Is ? (result = execute_block_at_index_helper<Is>(config), true) : false) || ...);
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
        
        // Initialize block stats with names (only if detailed stats enabled)
        void initialize_block_stats() {
            if (_config.collect_detailed_stats) {
                init_stats_impl(std::make_index_sequence<_N>{});
            }
        }
        
        // Helper for conditional timing
        auto get_time_if_needed(bool collect_stats) {
            return collect_stats ? std::chrono::high_resolution_clock::now() : 
                                 std::chrono::high_resolution_clock::time_point{};
        }
        
        
        template<size_t MaxBlocksParam, size_t MaxWorkers = DEFAULT_MAX_WORKERS>
        class FixedThreadPoolScheduler {
            using block_index_t = uint8_t;
            
            // Compile-time validation
            static_assert(MaxBlocksParam >= 1, "Must support at least one block");
            static_assert(MaxWorkers >= 1, "Must support at least one worker");
            static_assert(MaxBlocksParam <= (std::numeric_limits<block_index_t>::max)(), 
                          "MaxBlocksParam exceeds block_index_t capacity");
            
            // Align to cache line to prevent false sharing
            struct alignas(platform::cache_line_size) WorkerQueue {
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
            
            // Ensure WorkerQueue doesn't grow too large for cache efficiency
            static_assert(sizeof(WorkerQueue) <= platform::cache_line_size * 4, 
                          "WorkerQueue is too large, consider reducing MaxBlocksParam");
            
            std::array<WorkerQueue, MaxWorkers> queues;
            std::array<size_t, MaxBlocksParam> block_owner;  // Track which worker owns each block
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
                    block_owner[i] = worker;  // Track ownership for stats finalization
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
            
            bool is_block_owner(size_t worker_id, size_t block_idx) const {
                return block_idx < num_blocks && block_owner[block_idx] == worker_id;
            }
        };
        
        
        FixedThreadPoolScheduler<MaxBlocks, DEFAULT_MAX_WORKERS> fixed_thread_pool_scheduler;
        
        // FixedThreadPool implementation
        void run_fixed_thread_pool(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);

            // Validate worker count - must be at least 2 for fixed thread pool scheduling
            size_t num_workers = config.num_workers;
            assert(num_workers >= 2 && "FixedThreadPoolScheduler requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");
            
            // Initialize stats for all blocks
            initialize_block_stats();
            
            // If more workers than blocks, use thread-per-block scheduling
            if (num_workers >= _N) {
                // More workers than blocks - use thread-per-block (current behavior)
                run_thread_per_block(config);
            } else {
                // Fewer workers than blocks - use fixed thread pool scheduler
                // Initialize fixed thread pool scheduler with round-robin block distribution
                fixed_thread_pool_scheduler.initialize(_N, num_workers);
                
                // Record start time for all blocks (only if detailed stats enabled)
                if (config.collect_detailed_stats) {
                    auto start_time = std::chrono::high_resolution_clock::now();
                    for (size_t i = 0; i < _N; ++i) {
                        _block_start_times[i] = start_time;
                    }
                }
                
                // Create worker tasks using fixed thread pool scheduler
                _active_task_count = 0;
                for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                    _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, config]() {
                        run_fixed_thread_pool_worker(worker_id, config);
                    });
                    _active_task_count++;
                }
            }
        }
        
    public:  // Making run_fixed_thread_pool_worker public for lambda access (see comment above)
        void run_fixed_thread_pool_worker(size_t worker_id, const FlowGraphConfig& config) {
            while (!_stop_flag) {
                bool did_work = false;
                size_t block_idx;

                // Get next block from fixed thread pool scheduler (super fast - no atomics!)
                while (fixed_thread_pool_scheduler.get_next_block(worker_id, block_idx)) {
                    if (_stop_flag) break;
                    
                    // Track timing for dead time calculation (only if detailed stats enabled)
                    auto t_before = get_time_if_needed(config.collect_detailed_stats);
                    
                    bool block_did_work = execute_block_at_index(block_idx, config);
                    
                    // Update dead time if block failed to process
                    if (!block_did_work && config.collect_detailed_stats) {
                        auto t_after = std::chrono::high_resolution_clock::now();
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
            
            // Finalize stats when worker exits (only if detailed stats enabled)
            if (config.collect_detailed_stats) {
                auto end_time = std::chrono::high_resolution_clock::now();
                // Update stats only for blocks owned by this worker (fixes race condition)
                for (size_t i = 0; i < _N; ++i) {
                    if (fixed_thread_pool_scheduler.is_block_owner(worker_id, i)) {
                        std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                        _stats[i].total_runtime_s = total_runtime.count();
                        _stats[i].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[i].current_adaptive_sleep_us.load() : 0.0;
                    }
                }
            }
        }
        
    private:  // Return to private section for internal implementation details
        template<size_t I>
        bool execute_block_at_index_helper(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");
            
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];
            
            // Execute procedure and handle errors
            auto result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            
            if (result.is_err()) {
                if (config.collect_detailed_stats) {
                    stats.failed_procedures++;
                }
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
                if (config.collect_detailed_stats) {
                    stats.successful_procedures++;
                }
                
                // Handle adaptive sleep for successful procedure
                handle_adaptive_sleep(I, true);
                
                return true;
            }
        }
        
        bool execute_block_at_index(size_t index, const FlowGraphConfig& config) {
            // Runtime dispatch to compile-time template using C++17 compatible approach
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        
    };

    constexpr float PI = 3.14159265358979323846f;

} // namespace cler