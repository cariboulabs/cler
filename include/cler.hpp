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
        NotEnoughSamples,
        NotEnoughSpace,
        ProcedureError,
        BadData,
        TERMINATE_FLOWGRAPH,
        TERM_InvalidChannelIndex,
        TERM_ProcedureError,
        TERM_IOError,
        TERM_EOFReached,
    };

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
        size_t num_workers = 4;  // Number of worker threads (minimum 2, ignored for ThreadPerBlock)

        // Optimizes CPU usage, usually at the cost of reducing throughput
        // Most useful for:
        // - Intermittent sensor data  
        // - Network packet processing with gaps
        // - File processing with I/O delays
        bool adaptive_sleep = false;
        double adaptive_sleep_multiplier = 1.5;       // How aggressively to increase sleep time
        double adaptive_sleep_max_us = 5000.0;        // Maximum sleep time in microseconds
        size_t adaptive_sleep_fail_threshold = 10;    // Start sleeping after N consecutive fails
        
        // Dynamic work redistribution
        //especially useful for:
        // - Imbalanced workloads (where some paths or blocks are much slower)
        // - Dynamic data rates (where some blocks receive more data than others)
        bool load_balancing = false;
        size_t load_balancing_interval = 1000;     // Rebalance every N procedure calls
        double load_balancing_threshold = 0.2;     // 20% imbalance triggers rebalancing
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        typedef void (*OnErrTerminateCallback)(void* context);

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
            _stop_flag = false;
            
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
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            if (total_us > 100000) { // More than 100ms
                // Sleep for most of the duration, leaving 50ms for precise timing
                TaskPolicy::sleep_us(total_us - 50000);
            }
            
            // Use yield for the remaining time for precise timing
            while (std::chrono::steady_clock::now() - start_time < duration) {
                TaskPolicy::yield();
            }
            
            // Stop the flowgraph
            stop();
        }

        void stop() {
            _stop_flag = true;
            for (auto& t : _tasks) {
                TaskPolicy::join_task(t);
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_seq_cst);
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
                        stats.current_adaptive_sleep_us.store(1.0);
                        TaskPolicy::sleep_us(1);
                    } else {
                        // Exponential backoff
                        double new_sleep = std::min(
                            current_sleep * _config.adaptive_sleep_multiplier,
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
        void run_thread_per_block(const FlowGraphConfig& config) {
            // Launch one thread per block (original behavior)
            auto launch_helper = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((_tasks[Is] = TaskPolicy::create_task([this]() {
                auto& runner = std::get<Is>(_runners);
                auto& stats  = _stats[Is];

                stats.name = runner.block->name();

                auto t_start = std::chrono::steady_clock::now();
                auto t_last  = t_start;

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

                        if (err > Error::TERMINATE_FLOWGRAPH) {
                            _stop_flag = true;
                            if (_on_err_terminate_cb) {
                                _on_err_terminate_cb(_on_err_terminate_context);
                            }
                            return;
                        }

                        if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                            total_dead_time_s += dt.count();
                            // Use block-centric adaptive sleep
                            handle_adaptive_sleep(Is, false);
                        } else {
                            TaskPolicy::yield();
                        }

                    } else {
                        successful++;
                        // Use block-centric adaptive sleep  
                        handle_adaptive_sleep(Is, true);
                    }
                }

                auto t_end = std::chrono::steady_clock::now();
                std::chrono::duration<double> total_runtime_s = t_end - t_start;

                stats.successful_procedures = successful;
                stats.failed_procedures = failed;
                stats.total_dead_time_s = total_dead_time_s;
                stats.final_adaptive_sleep_us = _config.adaptive_sleep ? stats.current_adaptive_sleep_us.load() : 0.0;
                stats.total_runtime_s = total_runtime_s.count();
            })), ...);
            };
            launch_helper(std::make_index_sequence<_N>{});
        }

        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag = false;
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        OnErrTerminateCallback _on_err_terminate_cb = nullptr;
        void* _on_err_terminate_context = nullptr;
        std::array<std::chrono::steady_clock::time_point, _N> _block_start_times;
        
        // Initialize block stats with names
        void initialize_block_stats() {
            auto init_helper = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_stats[Is].name = std::get<Is>(_runners).block->name()), ...);
            };
            init_helper(std::make_index_sequence<_N>{});
        }
        
        // Adaptive Load Balancer
        template<size_t MaxBlocks = _N, size_t MaxWorkers = 8>
        class AdaptiveLoadBalancer {
        public:
            struct BlockMetrics {
                std::atomic<uint64_t> total_time_ns{0};
                std::atomic<uint64_t> successful_calls{0};
                std::atomic<uint64_t> samples_processed{0};
                
                double get_avg_time_per_call() const {
                    uint64_t calls = successful_calls.load();
                    return calls > 0 ? double(total_time_ns.load()) / calls : 0.0;
                }
                
                double get_load_weight() const {
                    return get_avg_time_per_call();
                }
            };
            
            
        private:
            std::array<BlockMetrics, MaxBlocks> block_metrics;
            std::array<std::atomic<size_t>, MaxWorkers> worker_iteration_count;
            
            // Static assignment arrays - embedded-safe
            std::array<std::array<size_t, MaxBlocks>, MaxWorkers> worker_assignments;
            std::array<std::atomic<size_t>, MaxWorkers> assignment_counts;
            
            size_t num_blocks = 0;
            size_t num_workers = 0;
            
        public:
            void initialize(size_t blocks, size_t workers) {
                num_blocks = std::min(blocks, MaxBlocks);
                num_workers = std::min(workers, MaxWorkers);
                
                // Initialize assignment counts to zero
                for (size_t w = 0; w < num_workers; ++w) {
                    assignment_counts[w].store(0);
                    worker_iteration_count[w].store(0);
                }
                
                // Initial round-robin assignment
                for (size_t i = 0; i < num_blocks; ++i) {
                    size_t worker_id = i % num_workers;
                    size_t count = assignment_counts[worker_id].load();
                    worker_assignments[worker_id][count] = i;
                    assignment_counts[worker_id]++;
                }
            }
            
            void update_block_metrics(size_t block_idx, uint64_t time_ns, uint64_t samples) {
                if (block_idx >= num_blocks) return;
                
                block_metrics[block_idx].total_time_ns += time_ns;
                block_metrics[block_idx].successful_calls++;
                block_metrics[block_idx].samples_processed += samples;
            }
            
            size_t get_worker_assignments(size_t worker_id, size_t* assignments_out, size_t max_assignments) {
                if (worker_id >= num_workers) return 0;
                
                size_t count = assignment_counts[worker_id].load();
                size_t copy_count = std::min(count, max_assignments);
                
                for (size_t i = 0; i < copy_count; ++i) {
                    assignments_out[i] = worker_assignments[worker_id][i];
                }
                
                return copy_count;
            }
            
            bool should_rebalance(size_t worker_id, size_t interval) {
                worker_iteration_count[worker_id]++;
                
                // Only worker 0 triggers rebalancing to avoid races
                if (worker_id == 0 && worker_iteration_count[worker_id] % interval == 0) {
                    return true;
                }
                return false;
            }
            
            void rebalance_workers(double threshold) {
                // Calculate load weights for each block
                std::array<double, MaxBlocks> block_weights;
                double total_weight = 0.0;
                
                for (size_t i = 0; i < num_blocks; ++i) {
                    block_weights[i] = block_metrics[i].get_load_weight();
                    total_weight += block_weights[i];
                }
                
                if (total_weight < 1e-9) return; // No meaningful data yet
                
                // Calculate current worker loads
                std::array<double, MaxWorkers> current_loads;
                for (size_t w = 0; w < num_workers; ++w) {
                    current_loads[w] = 0.0;
                    size_t count = assignment_counts[w].load();
                    for (size_t i = 0; i < count; ++i) {
                        size_t block_idx = worker_assignments[w][i];
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
            void rebalance_greedy(const std::array<double, MaxBlocks>& block_weights) {
                // Clear current assignments
                for (size_t w = 0; w < num_workers; ++w) {
                    assignment_counts[w] = 0;
                }
                
                // Create sorted block list (heaviest first)
                std::array<size_t, MaxBlocks> sorted_blocks;
                for (size_t i = 0; i < num_blocks; ++i) {
                    sorted_blocks[i] = i;
                }
                std::sort(sorted_blocks.begin(), sorted_blocks.begin() + num_blocks,
                    [&](size_t a, size_t b) { return block_weights[a] > block_weights[b]; });
                
                // Greedy assignment: always assign to least loaded worker
                std::array<double, MaxWorkers> worker_loads;
                worker_loads.fill(0.0);
                
                for (size_t i = 0; i < num_blocks; ++i) {
                    size_t block_idx = sorted_blocks[i];
                    // Find least loaded worker
                    auto min_it = std::min_element(worker_loads.begin(), 
                                                 worker_loads.begin() + num_workers);
                    size_t worker_id = min_it - worker_loads.begin();
                    
                    // Assign block to this worker
                    size_t count = assignment_counts[worker_id].load();
                    worker_assignments[worker_id][count] = block_idx;
                    assignment_counts[worker_id]++;
                    worker_loads[worker_id] += block_weights[block_idx];
                }
            }
        };
        
        AdaptiveLoadBalancer<32, 8> load_balancer;  // Static size for embedded compatibility
        
        // Enhanced scheduling implementations
        void run_with_load_balancing(const FlowGraphConfig& config) {
            _stop_flag = false;
            
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
                std::array<size_t, 32> assignments;  // Max 32 blocks per worker
                size_t assignment_count = load_balancer.get_worker_assignments(worker_id, assignments.data(), 32);
                
                // Process assigned blocks
                for (size_t i = 0; i < assignment_count; ++i) {
                    size_t block_idx = assignments[i];
                    if (_stop_flag) break;
                    
                    // Time the execution for load balancing metrics
                    auto start_time = std::chrono::high_resolution_clock::now();
                    
                    bool block_did_work = execute_block_at_index_with_metrics(block_idx, config);
                    did_work = did_work || block_did_work;
                    
                    // Update metrics for load balancing
                    if (block_did_work) {
                        auto end_time = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
                        load_balancer.update_block_metrics(block_idx, duration.count(), 1); // Assume 1 sample processed
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
            std::array<size_t, 32> final_assignments;
            size_t final_count = load_balancer.get_worker_assignments(worker_id, final_assignments.data(), 32);
            for (size_t i = 0; i < final_count; ++i) {
                size_t block_idx = final_assignments[i];
                std::chrono::duration<double> total_runtime = end_time - _block_start_times[block_idx];
                _stats[block_idx].total_runtime_s = total_runtime.count();
                _stats[block_idx].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[block_idx].current_adaptive_sleep_us.load() : 0.0;
            }
        }
        
        bool execute_block_at_index_with_metrics(size_t index, const FlowGraphConfig& config) {
            // Similar to execute_block_at_index but with metrics tracking
            bool result = false;
            auto dispatch = [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((index == Is ? (result = execute_block_at_index_helper<Is>(config), true) : false) || ...);
            };
            dispatch(std::make_index_sequence<_N>{});
            return result;
        }
        void run_with_thread_pool(const FlowGraphConfig& config) {
            _stop_flag = false;
            
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
            if constexpr (I >= _N) {
                return false;  // Invalid index
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
                if (err > Error::TERMINATE_FLOWGRAPH) {
                    _stop_flag = true;
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
            // Runtime dispatch to compile-time template
            bool result = false;
            auto dispatch = [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((index == Is ? (result = execute_block_at_index_helper<Is>(config), true) : false) || ...);
            };
            dispatch(std::make_index_sequence<_N>{});
            return result;
        }
    };

    constexpr size_t DEFAULT_BUFFER_SIZE = 256;
    constexpr float PI = 3.14159265358979323846f;

} // namespace cler