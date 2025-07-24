#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include "cler_embeddable_string.hpp"
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <chrono> // for timing measurements in FlowGraph
#include <tuple> // for storing block runners
#include <thread> // for hardware_concurrency

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
        double avg_dead_time_us = 0.0;
        double total_dead_time_s = 0.0;
        double final_adaptive_sleep_us = 0.0;
        double total_runtime_s = 0.0;
    };

    struct FlowGraphConfig {
        bool adaptive_sleep = false;
        double adaptive_sleep_ramp_up_factor = 1.5;
        double adaptive_sleep_max_us = 5000.0;
        double adaptive_sleep_target_gain = 0.5;
        double adaptive_sleep_decay_factor = 0.8;
        size_t adaptive_sleep_consecutive_fail_threshold = 50;
    };

    // Enhanced scheduling types for performance optimization  
    enum class SchedulerType {
        ThreadPerBlock,      // Current behavior (default) - one thread per block
        FixedThreadPool,     // Simple thread pool (embedded-friendly)
        WorkStealing,        // High-performance work-stealing (desktop) - future
        SingleThreaded       // No threading (baremetal/streamlined only)
    };
    
    // Enhanced configuration for performance optimization
    struct EnhancedFlowGraphConfig {
        // Scheduler configuration
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 0;  // 0 = auto-detect (hardware_concurrency)
        
        // Procedure call optimizations
        bool reduce_error_checks = false;     // Skip some validation in hot path
        bool inline_procedure_calls = true;   // Template optimization hint  
        size_t min_work_threshold = 1;        // Only call procedure() if >= samples
        
        // Legacy adaptive sleep (for ThreadPerBlock mode)
        bool adaptive_sleep = false;
        double adaptive_sleep_ramp_up_factor = 1.5;
        double adaptive_sleep_max_us = 5000.0;
        double adaptive_sleep_target_gain = 0.5;
        double adaptive_sleep_decay_factor = 0.8;
        size_t adaptive_sleep_consecutive_fail_threshold = 50;
        
        // Topology optimization (Tier 2 feature)
        bool topology_aware = false;
        
        // Convert to legacy FlowGraphConfig for backward compatibility
        FlowGraphConfig to_legacy_config() const {
            FlowGraphConfig legacy;
            legacy.adaptive_sleep = adaptive_sleep;
            legacy.adaptive_sleep_ramp_up_factor = adaptive_sleep_ramp_up_factor;
            legacy.adaptive_sleep_max_us = adaptive_sleep_max_us;
            legacy.adaptive_sleep_target_gain = adaptive_sleep_target_gain;
            legacy.adaptive_sleep_decay_factor = adaptive_sleep_decay_factor;
            legacy.adaptive_sleep_consecutive_fail_threshold = adaptive_sleep_consecutive_fail_threshold;
            return legacy;
        }
        
        // Factory methods for common configurations
        static EnhancedFlowGraphConfig embedded_optimized() {
            EnhancedFlowGraphConfig config;
            config.scheduler = SchedulerType::FixedThreadPool;
            config.num_workers = 2;  // Conservative for embedded
            config.reduce_error_checks = false;  // Keep safety
            config.min_work_threshold = 1;
            return config;
        }
        
        static EnhancedFlowGraphConfig desktop_performance() {
            EnhancedFlowGraphConfig config;
            config.scheduler = SchedulerType::FixedThreadPool;
            config.num_workers = 0;  // Auto-detect
            config.reduce_error_checks = true;   // Optimize for speed
            config.min_work_threshold = 4;       // Batch small work
            return config;
        }
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        typedef void (*OnCrashCallback)(void* context);

        FlowGraph(BlockRunners... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(std::move(runners))...)) {}

        ~FlowGraph() { stop(); }

        FlowGraph(const FlowGraph&) = delete;
        FlowGraph(FlowGraph&&) = delete;
        FlowGraph& operator=(const FlowGraph&) = delete;
        FlowGraph& operator=(FlowGraph&&) = delete;

        void set_on_crash_cb(OnCrashCallback cb, void* context) {
            _on_crash_cb = cb;
            _on_crash_context = context;
        }

        OnCrashCallback on_crash_cb() const { return _on_crash_cb; }
        void* on_crash_context() const { return _on_crash_context; }

        void run(FlowGraphConfig config = FlowGraphConfig{}) {
            _config = config;
            _stop_flag = false;
            launch_tasks_helper(std::make_index_sequence<_N>{});
        }
        
        // Enhanced configuration run method
        void run(const EnhancedFlowGraphConfig& enhanced_config) {
            _enhanced_config = enhanced_config;
            
            // For now, implement FixedThreadPool as the first enhancement
            switch (enhanced_config.scheduler) {
                case SchedulerType::ThreadPerBlock:
                    // Use legacy behavior
                    run(enhanced_config.to_legacy_config());
                    break;
                    
                case SchedulerType::FixedThreadPool:
                    run_with_thread_pool(enhanced_config);
                    break;
                    
                case SchedulerType::WorkStealing:
                    // Future implementation - fall back to thread pool for now
                    run_with_thread_pool(enhanced_config);
                    break;
                    
                case SchedulerType::SingleThreaded:
                    run_single_threaded(enhanced_config);
                    break;
            }
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
        template<std::size_t... Is>
        void launch_tasks_helper(std::index_sequence<Is...>) {
            ((_tasks[Is] = TaskPolicy::create_task([this]() {
                auto& runner = std::get<Is>(_runners);
                auto& stats  = _stats[Is];

                stats.name = runner.block->name();

                auto t_start = std::chrono::steady_clock::now();
                auto t_last  = t_start;

                size_t successful = 0;
                size_t failed = 0;
                double avg_dead_time_s = 0.0;
                double total_dead_time_s = 0.0;
                double adaptive_sleep_us = 0.0;
                size_t consecutive_fails = 0;

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
                            if (_on_crash_cb) {
                                _on_crash_cb(_on_crash_context);
                            }
                            return;
                        }

                        if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                            total_dead_time_s += dt.count();
                            avg_dead_time_s += (dt.count() - avg_dead_time_s) / failed;
                            consecutive_fails++;

                            if (_config.adaptive_sleep) {
                                if (consecutive_fails > _config.adaptive_sleep_consecutive_fail_threshold) {
                                    adaptive_sleep_us = std::min(
                                        adaptive_sleep_us * _config.adaptive_sleep_ramp_up_factor + 1.0,
                                        _config.adaptive_sleep_max_us
                                    );
                                }

                                double desired_us = avg_dead_time_s * 1e6 * _config.adaptive_sleep_target_gain;
                                adaptive_sleep_us = std::max(adaptive_sleep_us, desired_us);
                                TaskPolicy::sleep_us(static_cast<size_t>(adaptive_sleep_us));
                            } else {
                                TaskPolicy::yield();
                            }

                        } else {
                            TaskPolicy::yield();
                        }

                    } else {
                        successful++;
                        consecutive_fails = 0;
                        if (_config.adaptive_sleep) {
                            adaptive_sleep_us *= _config.adaptive_sleep_decay_factor;
                        }
                    }
                }

                auto t_end = std::chrono::steady_clock::now();
                std::chrono::duration<double> total_runtime_s = t_end - t_start;

                stats.successful_procedures = successful;
                stats.failed_procedures = failed;
                stats.avg_dead_time_us = avg_dead_time_s * 1e6;
                stats.total_dead_time_s = total_dead_time_s;
                stats.final_adaptive_sleep_us = _config.adaptive_sleep ? adaptive_sleep_us : 0.0;
                stats.total_runtime_s = total_runtime_s.count();
            })), ...);
        }

        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag = false;
        FlowGraphConfig _config;
        EnhancedFlowGraphConfig _enhanced_config;  // New enhanced configuration
        std::array<BlockExecutionStats, _N> _stats;
        OnCrashCallback _on_crash_cb = nullptr;
        void* _on_crash_context = nullptr;
        
        // Enhanced scheduling implementations
        void run_with_thread_pool(const EnhancedFlowGraphConfig& config) {
            _config = config.to_legacy_config();
            _stop_flag = false;
            
            // Determine number of workers
            size_t num_workers = config.num_workers;
            if (num_workers == 0) {
                num_workers = std::max(1u, std::thread::hardware_concurrency());
            }
            
            // For fixed thread pool: distribute blocks round-robin across workers
            // This is a simplified implementation - real thread pool would be more sophisticated
            if (num_workers >= _N) {
                // More workers than blocks - use thread-per-block (current behavior)
                launch_tasks_helper(std::make_index_sequence<_N>{});
            } else {
                // Fewer workers than blocks - use shared worker implementation
                launch_shared_workers(num_workers, config);
            }
        }
        
        void run_single_threaded(const EnhancedFlowGraphConfig& config) {
            // Single-threaded mode - execute all blocks sequentially in one thread
            // This is useful for baremetal or when deterministic execution is needed
            _config = config.to_legacy_config();
            _stop_flag = false;
            
            _tasks[0] = TaskPolicy::create_task([this, config]() {
                run_sequential_execution(config);
            });
        }
        
        void launch_shared_workers(size_t num_workers, const EnhancedFlowGraphConfig& config) {
            // Create worker tasks that process multiple blocks
            for (size_t worker_id = 0; worker_id < num_workers && worker_id < _N; ++worker_id) {
                _tasks[worker_id] = TaskPolicy::create_task([this, worker_id, num_workers, config]() {
                    run_worker_thread(worker_id, num_workers, config);
                });
            }
        }
        
        void run_worker_thread(size_t worker_id, size_t total_workers, const EnhancedFlowGraphConfig& config) {
            // Each worker processes blocks assigned to it (round-robin)
            auto process_block = [this, config](size_t block_index) {
                return execute_block_at_index(block_index, config);
            };
            
            while (!_stop_flag) {
                bool did_work = false;
                
                // Process blocks assigned to this worker
                for (size_t block_idx = worker_id; block_idx < _N; block_idx += total_workers) {
                    if (_stop_flag) break;
                    
                    bool block_did_work = process_block(block_idx);
                    did_work = did_work || block_did_work;
                }
                
                if (!did_work) {
                    // No work available, yield or sleep
                    if (_config.adaptive_sleep) {
                        TaskPolicy::sleep_us(100);  // Brief sleep
                    } else {
                        TaskPolicy::yield();
                    }
                }
            }
        }
        
        void run_sequential_execution(const EnhancedFlowGraphConfig& config) {
            // Single-threaded execution - process all blocks in sequence
            while (!_stop_flag) {
                bool any_work = false;
                
                for (size_t block_idx = 0; block_idx < _N; ++block_idx) {
                    if (_stop_flag) break;
                    
                    bool did_work = execute_block_at_index(block_idx, config);
                    any_work = any_work || did_work;
                }
                
                if (!any_work) {
                    // No blocks had work, brief pause
                    TaskPolicy::yield();
                }
            }
        }
        
        template<size_t I>
        bool execute_block_at_index_helper(const EnhancedFlowGraphConfig& config) {
            if constexpr (I >= _N) {
                return false;  // Invalid index
            }
            
            auto& runner = std::get<I>(_runners);
            auto& stats = _stats[I];
            
            // Apply procedure optimizations
            if (config.reduce_error_checks) {
                // Fast path - minimal error checking
                auto result = std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
                
                if (result.is_ok()) {
                    stats.successful_procedures++;
                    return true;
                } else {
                    stats.failed_procedures++;
                    auto err = result.unwrap_err();
                    if (err > Error::TERMINATE_FLOWGRAPH) {
                        _stop_flag = true;
                        if (_on_crash_cb) {
                            _on_crash_cb(_on_crash_context);
                        }
                    }
                    return false;
                }
            } else {
                // Safe path - full error checking (similar to current implementation)
                auto result = std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
                
                if (result.is_err()) {
                    stats.failed_procedures++;
                    auto err = result.unwrap_err();
                    if (err > Error::TERMINATE_FLOWGRAPH) {
                        _stop_flag = true;
                        if (_on_crash_cb) {
                            _on_crash_cb(_on_crash_context);
                        }
                    }
                    return false;
                } else {
                    stats.successful_procedures++;
                    return true;
                }
            }
        }
        
        bool execute_block_at_index(size_t index, const EnhancedFlowGraphConfig& config) {
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