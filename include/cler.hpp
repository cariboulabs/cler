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

namespace cler {

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

    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, channel_to_base_t<Channels>...>;

    struct BlockExecutionStats {
        EmbeddableString<64> name;
        size_t successful_procedures = 0;
        size_t failed_procedures = 0;
        size_t samples_processed = 0;
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
        
        double get_throughput_samples_per_sec() const {
            return total_runtime_s > 0 ? samples_processed / total_runtime_s : 0.0;
        }
    };

    // Enhanced scheduling types for performance optimization  
    enum class SchedulerType {
        ThreadPerBlock,        //Best for small flowgraphs or debugging
        FixedThreadPool,       //Best for uniform workloads
        AdaptiveLoadBalancing  //Best for imbalanced workloads
    };
    
    // Configuration for performance optimization
    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        static constexpr size_t DEFAULT_NUM_WORKERS = 4;
        size_t num_workers = DEFAULT_NUM_WORKERS;  // Number of worker threads (minimum 2, ignored for ThreadPerBlock)

        // Optimizes CPU usage, usually at the cost of reducing throughput
        // Most useful for:
        // - Intermittent sensor data  
        // - Network packet processing with gaps
        // - File processing with I/O delays
        bool adaptive_sleep = false;
        static constexpr double DEFAULT_ADAPTIVE_SLEEP_MULTIPLIER = 1.5;
        static constexpr double DEFAULT_ADAPTIVE_SLEEP_MAX_US = 5000.0;
        static constexpr size_t DEFAULT_ADAPTIVE_SLEEP_FAIL_THRESHOLD = 10;
        
        double adaptive_sleep_multiplier = DEFAULT_ADAPTIVE_SLEEP_MULTIPLIER;  // How aggressively to increase sleep time
        double adaptive_sleep_max_us = DEFAULT_ADAPTIVE_SLEEP_MAX_US;          // Maximum sleep time in microseconds
        size_t adaptive_sleep_fail_threshold = DEFAULT_ADAPTIVE_SLEEP_FAIL_THRESHOLD;  // Start sleeping after N consecutive fails
        
        // Dynamic work redistribution
        //especially useful for:
        // - Imbalanced workloads (where some paths or blocks are much slower)
        // - Dynamic data rates (where some blocks receive more data than others)
        bool load_balancing = false;
        static constexpr size_t DEFAULT_LOAD_BALANCING_INTERVAL = 1000;
        static constexpr double DEFAULT_LOAD_BALANCING_THRESHOLD = 0.2;
        
        size_t load_balancing_interval = DEFAULT_LOAD_BALANCING_INTERVAL;  // Rebalance every N procedure calls
        double load_balancing_threshold = DEFAULT_LOAD_BALANCING_THRESHOLD; // 20% imbalance triggers rebalancing
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        static constexpr std::size_t MaxBlocks = sizeof...(BlockRunners);  // Clean compile-time constant
        static_assert(_N > 0, "FlowGraph must have at least one block");
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
                    run_with_thread_pool(config);
                    break;
                    
                case SchedulerType::AdaptiveLoadBalancing:
                    run_with_load_balancing(config);
                    break;
            }
        }
        
        template<typename Rep, typename Period>
        void run_for(const std::chrono::duration<Rep, Period>& duration, const FlowGraphConfig& config = FlowGraphConfig{}) {
            // Start the flowgraph
            auto start_time = std::chrono::steady_clock::now();
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
            while (std::chrono::steady_clock::now() - start_time < duration) {
                TaskPolicy::yield();
            }
            
            // Stop the flowgraph
            stop();
        }

        void stop() {
            _stop_flag.store(true, std::memory_order_release);
            for (auto& t : _tasks) {
                TaskPolicy::join_task(t);
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_acquire);
        }

        const FlowGraphConfig& config() const { return _config; }
        const std::array<BlockExecutionStats, _N>& stats() const { return _stats; }

    private:
        // Block-centric adaptive sleep logic (works with all schedulers)
        void handle_adaptive_sleep(size_t block_idx, bool procedure_succeeded) {
            if (!_config.adaptive_sleep) return;
            
            auto& stats = _stats[block_idx];
            
            if (procedure_succeeded) {
                // Reset sleep state on success
                stats.consecutive_fails.store(0);
                stats.current_adaptive_sleep_us.store(0.0);
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

            auto t_start = std::chrono::steady_clock::now();
            auto t_last = t_start;

            size_t successful = 0;
            size_t failed = 0;
            double total_dead_time_s = 0.0;

            while (!_stop_flag) {
                auto t_now = std::chrono::steady_clock::now();
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

            auto t_end = std::chrono::steady_clock::now();
            std::chrono::duration<double> total_runtime_s = t_end - t_start;

            stats.successful_procedures = successful;
            stats.failed_procedures = failed;
            stats.total_dead_time_s = total_dead_time_s;
            stats.final_adaptive_sleep_us = config.adaptive_sleep ? stats.current_adaptive_sleep_us.load() : 0.0;
            stats.total_runtime_s = total_runtime_s.count();
        }
        
        template<std::size_t... Is>
        void launch_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            ((_tasks[Is] = TaskPolicy::create_task([this, config]() {
                run_block_at_index_thread_per_block<Is>(config);
            })), ...);
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
        std::array<std::chrono::steady_clock::time_point, _N> _block_start_times;
        
        // Initialize block stats with names
        void initialize_block_stats() {
            init_stats_impl(std::make_index_sequence<_N>{});
        }
        
        // Adaptive Load Balancer
        static constexpr size_t DEFAULT_MAX_WORKERS = 8;
        template<size_t MaxBlocksParam, size_t MaxWorkers = DEFAULT_MAX_WORKERS>
        class AdaptiveLoadBalancer {
        public:
            struct BlockMetrics {
                std::atomic<uint64_t> total_time_ns{0};
                std::atomic<uint64_t> successful_calls{0};
                
                double get_avg_time_per_call() const {
                    uint64_t calls = successful_calls.load();
                    return calls > 0 ? double(total_time_ns.load()) / calls : 0.0;
                }
                
                double get_load_weight() const {
                    return get_avg_time_per_call();
                }
            };
            
            
        private:
            std::array<BlockMetrics, MaxBlocksParam> block_metrics;
            std::array<std::atomic<size_t>, MaxWorkers> worker_iteration_count;
            
            // Double-buffered assignment arrays to prevent race conditions
            std::array<std::array<size_t, MaxBlocksParam>, MaxWorkers> worker_assignments_a;
            std::array<std::array<size_t, MaxBlocksParam>, MaxWorkers> worker_assignments_b;
            std::array<std::atomic<size_t>, MaxWorkers> assignment_counts_a;
            std::array<std::atomic<size_t>, MaxWorkers> assignment_counts_b;
            std::atomic<bool> use_buffer_a{true};  // Which buffer is currently active for reading
            
            size_t num_blocks = 0;
            size_t num_workers = 0;
            
        public:
            void initialize(size_t blocks, size_t workers) {
                num_blocks = std::min(blocks, MaxBlocksParam);
                num_workers = std::min(workers, MaxWorkers);
                
                // Runtime validation for embedded safety
                assert(num_workers > 0 && "Must have at least one worker");
                assert(num_blocks > 0 && "Must have at least one block");
                assert(num_workers <= MaxWorkers && "Worker count exceeds template parameter");
                assert(num_blocks <= MaxBlocksParam && "Block count exceeds template parameter");
                
                // Initialize both assignment buffers to zero
                for (size_t w = 0; w < num_workers; ++w) {
                    assignment_counts_a[w].store(0);
                    assignment_counts_b[w].store(0);
                    worker_iteration_count[w].store(0);
                }
                
                // Initial round-robin assignment to buffer A
                for (size_t i = 0; i < num_blocks; ++i) {
                    size_t worker_id = i % num_workers;
                    size_t count = assignment_counts_a[worker_id].load();
                    worker_assignments_a[worker_id][count] = i;
                    assignment_counts_a[worker_id]++;
                }
                
                // Copy to buffer B for consistency
                for (size_t w = 0; w < num_workers; ++w) {
                    size_t count = assignment_counts_a[w].load();
                    assignment_counts_b[w].store(count);
                    for (size_t i = 0; i < count; ++i) {
                        worker_assignments_b[w][i] = worker_assignments_a[w][i];
                    }
                }
                
                use_buffer_a.store(true);
            }
            
            void update_block_metrics(size_t block_idx, uint64_t time_ns) {
                if (block_idx >= num_blocks) return;
                
                block_metrics[block_idx].total_time_ns += time_ns;
                block_metrics[block_idx].successful_calls++;
            }
            
            size_t get_worker_assignments(size_t worker_id, size_t* assignments_out, size_t max_assignments) {
                // Compile-time safeguards for embedded systems  
                // Each worker's assignment array can hold MaxBlocksParam blocks
                // In worst case, one worker gets all blocks, so MaxBlocksParam must be >= total blocks
                static_assert(MaxWorkers >= 1, "Must have at least one worker");
                static_assert(MaxBlocksParam >= 1, "Must have at least one block");
                // Note: The capacity constraint is inherently satisfied since MaxBlocksParam = total blocks
                
                if (worker_id >= num_workers) return 0;
                
                // Use atomic read to determine which buffer is active
                bool use_a = use_buffer_a.load(std::memory_order_acquire);
                
                size_t count, copy_count;
                if (use_a) {
                    count = assignment_counts_a[worker_id].load();
                    copy_count = std::min(count, max_assignments);
                    for (size_t i = 0; i < copy_count; ++i) {
                        assignments_out[i] = worker_assignments_a[worker_id][i];
                    }
                } else {
                    count = assignment_counts_b[worker_id].load();
                    copy_count = std::min(count, max_assignments);
                    for (size_t i = 0; i < copy_count; ++i) {
                        assignments_out[i] = worker_assignments_b[worker_id][i];
                    }
                }
                
                return copy_count;
            }
            
            bool should_rebalance(size_t worker_id, size_t interval) {
                worker_iteration_count[worker_id]++;
                
                // Distributed rebalance triggering to prevent worker 0 starvation
                // Each worker can trigger, but offset by worker_id to stagger timing
                // This ensures at least one worker will trigger even if others are starved
                if ((worker_iteration_count[worker_id] + worker_id) % interval == 0) {
                    return true;
                }
                return false;
            }
            
            void rebalance_workers(double threshold) {
                // Calculate load weights for each block
                std::array<double, MaxBlocksParam> block_weights;
                double total_weight = 0.0;
                
                for (size_t i = 0; i < num_blocks; ++i) {
                    block_weights[i] = block_metrics[i].get_load_weight();
                    total_weight += block_weights[i];
                }
                
                static constexpr double MIN_MEANINGFUL_WEIGHT = 1e-9;
                if (total_weight < MIN_MEANINGFUL_WEIGHT) return; // No meaningful data yet
                
                // Calculate current worker loads from active buffer
                bool use_a = use_buffer_a.load(std::memory_order_acquire);
                auto& read_assignments = use_a ? worker_assignments_a : worker_assignments_b;
                auto& read_counts = use_a ? assignment_counts_a : assignment_counts_b;
                
                std::array<double, MaxWorkers> current_loads;
                for (size_t w = 0; w < num_workers; ++w) {
                    current_loads[w] = 0.0;
                    size_t count = read_counts[w].load();
                    for (size_t i = 0; i < count; ++i) {
                        size_t block_idx = read_assignments[w][i];
                        current_loads[w] += block_weights[block_idx];
                    }
                }
                
                // Check if rebalancing is needed
                double avg_load = total_weight / num_workers;
                double max_deviation = 0.0;
                for (size_t w = 0; w < num_workers; ++w) {
                    double deviation = std::abs(current_loads[w] - avg_load) / avg_load;
                    max_deviation = std::max(max_deviation, deviation);
                }
                
                if (max_deviation < threshold) return; // Already balanced
                
                // Perform greedy rebalancing
                rebalance_greedy(block_weights);
            }
            
        private:
            void rebalance_greedy(const std::array<double, MaxBlocksParam>& block_weights) {
                // Determine which buffer to write to (opposite of current active buffer)
                bool use_a = use_buffer_a.load(std::memory_order_acquire);
                auto& write_assignments = use_a ? worker_assignments_b : worker_assignments_a;
                auto& write_counts = use_a ? assignment_counts_b : assignment_counts_a;
                
                // Clear the write buffer assignments
                for (size_t w = 0; w < num_workers; ++w) {
                    write_counts[w].store(0);
                }
                
                // Create sorted block list (heaviest first) using bounds-safe approach
                std::array<size_t, MaxBlocksParam> sorted_blocks;
                // Initialize all elements to invalid value for safety
                sorted_blocks.fill(MaxBlocksParam); // Use MaxBlocksParam as sentinel (invalid index)
                for (size_t i = 0; i < num_blocks; ++i) {
                    sorted_blocks[i] = i;
                }
                
                // Use simple insertion sort for small arrays to avoid std::sort bounds issues
                // This is more efficient for small MaxBlocksParam anyway (typical embedded use)
                for (size_t i = 1; i < num_blocks; ++i) {
                    size_t key = sorted_blocks[i];
                    double key_weight = block_weights[key];
                    size_t j = i;
                    while (j > 0 && block_weights[sorted_blocks[j-1]] < key_weight) {
                        sorted_blocks[j] = sorted_blocks[j-1];
                        --j;
                    }
                    sorted_blocks[j] = key;
                }
                
                // Greedy assignment: always assign to least loaded worker
                std::array<double, MaxWorkers> worker_loads;
                worker_loads.fill(0.0);
                
                for (size_t i = 0; i < num_blocks; ++i) {
                    size_t block_idx = sorted_blocks[i];
                    
                    // Bounds checking - this should never fail but defensive programming
                    if (block_idx >= MaxBlocksParam) {
                        assert(false && "Invalid block index in rebalance_greedy");
                        continue; // Skip invalid assignment
                    }
                    
                    // Find least loaded worker
                    auto min_it = std::min_element(worker_loads.begin(), 
                                                 worker_loads.begin() + num_workers);
                    size_t worker_id = min_it - worker_loads.begin();
                    
                    // Assign block to this worker (write to inactive buffer)
                    size_t count = write_counts[worker_id].load();
                    write_assignments[worker_id][count] = block_idx;
                    write_counts[worker_id].store(count + 1);
                    worker_loads[worker_id] += block_weights[block_idx];
                }
                
                // Atomically swap buffers - this makes the new assignments visible
                use_buffer_a.store(!use_a, std::memory_order_release);
            }
        };
        
        AdaptiveLoadBalancer<MaxBlocks, DEFAULT_MAX_WORKERS> load_balancer;  // Clean template parameterization
        
        // Enhanced scheduling implementations
        void run_with_load_balancing(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            
            // Validate worker count - must be at least 2 for load balancing
            size_t num_workers = config.num_workers;
            assert(num_workers >= 2 && "AdaptiveLoadBalancing requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");
            
            // Initialize stats for all blocks
            initialize_block_stats();
            
            // Initialize load balancer
            load_balancer.initialize(_N, num_workers);
            
            // Record start time for all blocks
            auto start_time = std::chrono::steady_clock::now();
            for (size_t i = 0; i < _N; ++i) {
                _block_start_times[i] = start_time;
            }
            
            // Create worker tasks that use load balancer assignments
            for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, config]() {
                    run_load_balanced_worker(worker_id, config);
                });
            }
        }
        
        void run_load_balanced_worker(size_t worker_id, const FlowGraphConfig& config) {
            size_t iteration_count = 0;
            
            while (!_stop_flag) {
                bool did_work = false;
                
                // Get current block assignments for this worker
                std::array<size_t, MaxBlocks> assignments;  // Max MaxBlocks blocks per worker
                size_t assignment_count = load_balancer.get_worker_assignments(worker_id, assignments.data(), MaxBlocks);
                
                // Process assigned blocks
                for (size_t i = 0; i < assignment_count; ++i) {
                    size_t block_idx = assignments[i];
                    if (_stop_flag) break;
                    
                    // Bounds checking - critical for embedded safety
                    if (block_idx >= _N) {
                        // This should never happen, but defensive programming
                        assert(false && "Load balancer returned invalid block index");
                        continue; // Skip this invalid assignment
                    }
                    
                    // Time the execution for load balancing metrics
                    auto start_time = std::chrono::high_resolution_clock::now();
                    
                    bool block_did_work = execute_block_at_index_with_metrics(block_idx, config);
                    did_work = did_work || block_did_work;
                    
                    // Update metrics for load balancing
                    if (block_did_work) {
                        auto end_time = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
                        load_balancer.update_block_metrics(block_idx, duration.count());
                    } else {
                        // Track dead time
                        auto end_time = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<double> dt = end_time - start_time;
                        _stats[block_idx].total_dead_time_s += dt.count();
                    }
                }
                
                // Check if rebalancing is needed
                if (config.load_balancing && 
                    load_balancer.should_rebalance(worker_id, config.load_balancing_interval)) {
                    load_balancer.rebalance_workers(config.load_balancing_threshold);
                }
                
                if (!did_work) {
                    // No work available, yield to other workers
                    TaskPolicy::yield();
                }
                
                iteration_count++;
            }
            
            // Finalize stats when worker exits
            auto end_time = std::chrono::steady_clock::now();
            
            // Update stats for all blocks this worker processed
            std::array<size_t, MaxBlocks> final_assignments;
            size_t final_count = load_balancer.get_worker_assignments(worker_id, final_assignments.data(), MaxBlocks);
            for (size_t i = 0; i < final_count; ++i) {
                size_t block_idx = final_assignments[i];
                
                // Bounds checking - critical for embedded safety
                if (block_idx >= _N) {
                    // This should never happen, but defensive programming
                    assert(false && "Load balancer returned invalid block index in final stats");
                    continue; // Skip this invalid assignment
                }
                
                std::chrono::duration<double> total_runtime = end_time - _block_start_times[block_idx];
                _stats[block_idx].total_runtime_s = total_runtime.count();
                _stats[block_idx].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[block_idx].current_adaptive_sleep_us.load() : 0.0;
            }
        }
        
        bool execute_block_at_index_with_metrics(size_t index, const FlowGraphConfig& config) {
            // Similar to execute_block_at_index but with metrics tracking using C++17 compatible approach
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        void run_with_thread_pool(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            
            // Validate worker count - must be at least 2 for thread pooling
            size_t num_workers = config.num_workers;
            assert(num_workers >= 2 && "FixedThreadPool requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");
            
            // Initialize stats for all blocks
            initialize_block_stats();
            
            // For fixed thread pool: distribute blocks round-robin across workers
            // This is a simplified implementation - real thread pool would be more sophisticated
            if (num_workers >= _N) {
                // More workers than blocks - use thread-per-block (current behavior)
                run_thread_per_block(config);
            } else {
                // Fewer workers than blocks - use shared worker implementation
                launch_shared_workers(num_workers, config);
            }
        }
        
        void launch_shared_workers(size_t num_workers, const FlowGraphConfig& config) {
            // Record start time for all blocks
            auto start_time = std::chrono::steady_clock::now();
            
            // Store start time for stats calculation
            for (size_t i = 0; i < _N; ++i) {
                _block_start_times[i] = start_time;
            }
            
            // Create worker tasks that process multiple blocks
            for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, num_workers, config]() {
                    run_worker_thread(worker_id, num_workers, config);
                });
            }
        }
        
        void run_worker_thread(size_t worker_id, size_t total_workers, const FlowGraphConfig& config) {
            // Each worker processes blocks assigned to it (round-robin)
            while (!_stop_flag) {
                bool did_work = false;
                
                // Process blocks assigned to this worker
                for (size_t block_idx = worker_id; block_idx < _N; block_idx += total_workers) {
                    if (_stop_flag) break;
                    
                    // Track timing for dead time calculation
                    auto t_before = std::chrono::steady_clock::now();
                    bool block_did_work = execute_block_at_index(block_idx, config);
                    auto t_after = std::chrono::steady_clock::now();
                    
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
            auto end_time = std::chrono::steady_clock::now();
            for (size_t block_idx = worker_id; block_idx < _N; block_idx += total_workers) {
                std::chrono::duration<double> total_runtime = end_time - _block_start_times[block_idx];
                _stats[block_idx].total_runtime_s = total_runtime.count();
                _stats[block_idx].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[block_idx].current_adaptive_sleep_us.load() : 0.0;
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
    };

    constexpr size_t DEFAULT_BUFFER_SIZE = 256;
    constexpr float PI = 3.14159265358979323846f;

} // namespace cler