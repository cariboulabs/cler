#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include "cler_embeddable_string.hpp"
#include "cler_platform.hpp"
#include "task_policies/cler_task_policy_base.hpp"
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <chrono> // for timing measurements in FlowGraph
#include <tuple> // for storing block runners
#include <cassert> // for assertions
#include <atomic> // for atomic adaptive sleep state
#include <limits> // for std::numeric_limits
#include <type_traits>
#include <cstdint>

namespace cler {

    //here so we can insure blocks use this feature
    constexpr size_t DOUBLY_MAPPED_MIN_SIZE = dro::details::DOUBLY_MAPPED_MIN_SIZE;

    // Configurable at compile-time for different target platforms
    #ifndef CLER_DEFAULT_MAX_WORKERS
    #define CLER_DEFAULT_MAX_WORKERS (8)  // Conservative default for embedded systems
    #endif
    constexpr size_t DEFAULT_MAX_WORKERS = CLER_DEFAULT_MAX_WORKERS;

    constexpr size_t MAX_REGISTERED_INPUTS = 16;

    enum class Error {
        OK,

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
        virtual std::size_t producer_thread_cumulative_write_count() const = 0;
    };

    template <typename T, size_t N = 0>
    struct Channel : public ChannelBase<T> {
        dro::SPSCQueue<T, N> _queue;
        Channel() = default;

        template<size_t M = N, typename = std::enable_if_t<M == 0>>
        Channel(size_t size) : _queue(size) {
            assert(size > 0 && "Channel size must be greater than zero");
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
        std::size_t producer_thread_cumulative_write_count() const override {
            return _queue.producer_thread_cumulative_write_count();
        }
    };

    struct BlockBase {
        explicit BlockBase(const char* name) : _name(name) {}
        explicit BlockBase(const EmbeddableString<64>& name) : _name(name) {}
        const char* name() const { return _name.c_str(); }
        BlockBase(const BlockBase&) = delete;
        BlockBase& operator=(const BlockBase&) = delete;
        BlockBase(BlockBase&&) = delete;
        BlockBase& operator=(BlockBase&&) = delete;

        template<typename T>
        void register_input(ChannelBase<T>& channel) {
            assert(_registered_input_count < MAX_REGISTERED_INPUTS &&
                   "BlockBase::register_input: MAX_REGISTERED_INPUTS exceeded");
            if (_registered_input_count < MAX_REGISTERED_INPUTS) {
                _registered_inputs[_registered_input_count++] = static_cast<const void*>(&channel);
            }
        }

        size_t registered_input_count() const { return _registered_input_count; }
        const void* registered_input_at(size_t i) const { return _registered_inputs[i]; }

    private:
        EmbeddableString<64> _name;
        std::array<const void*, MAX_REGISTERED_INPUTS> _registered_inputs{};
        size_t _registered_input_count = 0;
    };

    template<typename T>
    struct channel_to_base { using type = T; };
    template<typename T, size_t N>
    struct channel_to_base<Channel<T, N>> { using type = ChannelBase<T>; };
    template<typename T>
    using channel_to_base_t = typename channel_to_base<T>::type;

    template<typename Block, typename = void>
    struct block_declares_may_block : std::false_type {};
    template<typename Block>
    struct block_declares_may_block<Block, std::enable_if_t<Block::may_block>> : std::true_type {};
    template<typename Block>
    constexpr bool block_declares_may_block_v = block_declares_may_block<Block>::value;

    template<typename Block, typename... Channels>
    struct BlockRunner {
        Block* block;
        std::tuple<Channels*...> outputs;
        bool may_block = block_declares_may_block_v<Block>;

        template<typename... InputChannels>
        BlockRunner(Block* blk, InputChannels*... outs)
            : block(blk), outputs(static_cast<Channels*>(outs)...) {}
    };

    // C++17 deduction guide: automatically deduces channel base types from concrete channel types
    // This allows: BlockRunner(&block, &channel) instead of BlockRunner<Block, ChannelBase<T>>(&block, &channel)
    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, channel_to_base_t<Channels>...>;

    template<typename Block, typename... Channels>
    auto BlockRunnerMayBlock(Block* blk, Channels*... outs) {
        BlockRunner<Block, channel_to_base_t<Channels>...> runner(blk, outs...);
        runner.may_block = true;
        return runner;
    }

    struct Edge {
        uint8_t producer;
        uint8_t consumer;
    };

    template<typename Runner>
    struct runner_output_count;
    template<typename Block, typename... Channels>
    struct runner_output_count<BlockRunner<Block, Channels...>> {
        static constexpr size_t value = sizeof...(Channels);
    };

    struct alignas(platform::cache_line_size) BlockExecutionStats {
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

    struct BlockCost {
        double ewma_ns_per_call = 0.0;
        double ewma_items_per_call = 0.0;
    };

    enum class SchedulerType {
        ThreadPerBlock,
        FixedThreadPool,
        PinnedIslands
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
        
        // Micro-batching: run block procedure multiple times per scheduling tick
        // Reduces context switches and queue crossing overhead
        // Typical values: 1-16, with 4-8 being sweet spot for most workloads
        size_t max_calls_per_tick = 4;  // Conservative default to prevent runaway hot stages
        
        // Optional: pin worker threads to specific CPU cores
        // Can improve cache locality and reduce migration overhead
        bool pin_workers = false;

        size_t calibration_ms = 500;
        size_t cpu_id_offset = 0;
        size_t park_after_zero_passes = 4;
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        static constexpr std::size_t MaxBlocks = sizeof...(BlockRunners);  // Clean compile-time constant
        static_assert(_N > 0, "FlowGraph must have at least one block");
        static_assert(_N <= 256, "FlowGraph cannot have more than 256 blocks (due to uint8_t indexing)");
        static constexpr std::size_t MaxEdges = (runner_output_count<BlockRunners>::value + ... + 0);
        using OnErrTerminateCallback = void (*)(void* context);

        struct UnresolvedEdge {
            uint8_t producer;
            const void* address;
        };

        struct Partition {
            std::array<uint8_t, _N> block_ids{};
            std::array<uint16_t, DEFAULT_MAX_WORKERS + 1> island_begin{};
            uint16_t block_count = 0;
            uint16_t island_count = 0;

            uint16_t island_size(size_t island) const {
                return static_cast<uint16_t>(island_begin[island + 1] - island_begin[island]);
            }

            size_t island_of(uint8_t block_id) const {
                for (size_t w = 0; w < island_count; ++w) {
                    for (uint16_t k = island_begin[w]; k < island_begin[w + 1]; ++k) {
                        if (block_ids[k] == block_id) return w;
                    }
                }
                return island_count;
            }
        };

        FlowGraph(BlockRunners... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(std::move(runners))...)) {
            derive_edges();
        }

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

                case SchedulerType::PinnedIslands:
                    run_pinned_islands(config);
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
            
            // Use relax for the remaining time for precise timing
            while (std::chrono::high_resolution_clock::now() - start_time < duration) {
                TaskPolicy::relax();
            }
            
            // Stop the flowgraph
            stop();
        }

        void stop() {
            _stop_flag.store(true, std::memory_order_release);
            if (_config.scheduler == SchedulerType::PinnedIslands) {
                unpark_everyone();
            }
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

        std::array<BlockCost, _N> block_costs() const {
            std::array<BlockCost, _N> out{};
            for (size_t i = 0; i < _N; ++i) {
                out[i].ewma_ns_per_call = _cost_samples[i].ewma_ns_per_call.load(std::memory_order_relaxed);
                out[i].ewma_items_per_call = _cost_samples[i].ewma_items_per_call.load(std::memory_order_relaxed);
            }
            return out;
        }

        const Partition& partition() const { return _partition; }
        size_t repartition_count() const { return _repartition_count.load(std::memory_order_acquire); }
        size_t affinity_failure_count() const { return _affinity_failures.load(std::memory_order_relaxed); }

        uint64_t total_park_events() const {
            uint64_t total = 0;
            for (const auto& state : _park_states) {
                total += state.park_events.load(std::memory_order_relaxed);
            }
            return total;
        }

        const std::array<Edge, MaxEdges>& edges() const { return _edges; }
        size_t edge_count() const { return _edge_count; }
        size_t unresolved_edge_count() const { return _unresolved_edge_count; }
        const std::array<UnresolvedEdge, MaxEdges>& unresolved_edges() const { return _unresolved_edges; }
        const char* block_name(size_t index) const { return _block_bases[index]->name(); }

    private:
        struct BlockSpan {
            const void* begin;
            const void* end;
        };

        template<std::size_t I>
        void collect_block_base_for_index() {
            _block_bases[I] = static_cast<const BlockBase*>(std::get<I>(_runners).block);
        }

        template<std::size_t... Is>
        void collect_block_bases_impl(std::index_sequence<Is...>) {
            (collect_block_base_for_index<Is>(), ...);
        }

        template<std::size_t I>
        void collect_span_for_index(std::array<BlockSpan, _N>& spans) {
            auto* block = std::get<I>(_runners).block;
            const void* begin = static_cast<const void*>(block);
            const void* end = static_cast<const void*>(
                reinterpret_cast<const unsigned char*>(begin) + sizeof(*block));
            spans[I] = BlockSpan{begin, end};
        }

        template<std::size_t... Is>
        void collect_spans_impl(std::index_sequence<Is...>, std::array<BlockSpan, _N>& spans) {
            (collect_span_for_index<Is>(spans), ...);
        }

        void resolve_and_add_edge(uint8_t producer, const void* address, const std::array<BlockSpan, _N>& spans) {
            for (size_t k = 0; k < _N; ++k) {
                const BlockBase* candidate = _block_bases[k];
                for (size_t r = 0; r < candidate->registered_input_count(); ++r) {
                    if (candidate->registered_input_at(r) == address) {
                        _edges[_edge_count++] = Edge{producer, static_cast<uint8_t>(k)};
                        return;
                    }
                }
            }
            for (size_t k = 0; k < _N; ++k) {
                if (address >= spans[k].begin && address < spans[k].end) {
                    _edges[_edge_count++] = Edge{producer, static_cast<uint8_t>(k)};
                    return;
                }
            }
            _unresolved_edges[_unresolved_edge_count++] = UnresolvedEdge{producer, address};
        }

        template<std::size_t I>
        void collect_edges_for_index(const std::array<BlockSpan, _N>& spans) {
            auto& runner = std::get<I>(_runners);
            std::apply([&](auto*... outs) {
                (resolve_and_add_edge(static_cast<uint8_t>(I), static_cast<const void*>(outs), spans), ...);
            }, runner.outputs);
        }

        template<std::size_t... Is>
        void collect_edges_impl(std::index_sequence<Is...>, const std::array<BlockSpan, _N>& spans) {
            (collect_edges_for_index<Is>(spans), ...);
        }

        void derive_edges() {
            collect_block_bases_impl(std::make_index_sequence<_N>{});
            std::array<BlockSpan, _N> spans{};
            collect_spans_impl(std::make_index_sequence<_N>{}, spans);
            collect_edges_impl(std::make_index_sequence<_N>{}, spans);
        }

        void handle_adaptive_sleep(size_t block_idx, bool procedure_succeeded) {
            if (!_config.adaptive_sleep) return;

            auto& stats = _stats[block_idx];

            if (procedure_succeeded) {
                stats.consecutive_fails.store(0);
                double current_sleep = stats.current_adaptive_sleep_us.load();
                stats.current_adaptive_sleep_us.store(current_sleep * 0.5);
            } else {
                size_t fails = stats.consecutive_fails.fetch_add(1) + 1;

                if (fails > _config.adaptive_sleep_fail_threshold) {
                    double current_sleep = stats.current_adaptive_sleep_us.load();
                    double new_sleep;

                    if (current_sleep == 0.0) {
                        static constexpr double INITIAL_SLEEP_US = 1.0;
                        new_sleep = INITIAL_SLEEP_US;
                    } else {
                        double base_sleep = current_sleep * _config.adaptive_sleep_multiplier;
                        static constexpr double JITTER_FACTOR = 0.1;
                        double block_jitter = 1.0 + JITTER_FACTOR * (double(block_idx % 10) / 10.0 - 0.5);
                        new_sleep = std::min(base_sleep * block_jitter, _config.adaptive_sleep_max_us);
                    }

                    stats.current_adaptive_sleep_us.store(new_sleep);
                }
            }
        }

        static constexpr size_t COST_SAMPLE_PERIOD_MASK = 63;

        struct alignas(platform::cache_line_size) SchedulerCostSample {
            size_t call_counter = 0;
            std::atomic<double> ewma_ns_per_call{0.0};
            std::atomic<double> ewma_items_per_call{0.0};
        };

        template<typename... Channels>
        static std::size_t sum_output_cumulative_write_count(const std::tuple<Channels*...>& outputs) {
            return std::apply([](auto*... outs) {
                return (std::size_t{0} + ... + outs->producer_thread_cumulative_write_count());
            }, outputs);
        }

        template<std::size_t I>
        Result<Empty, Error> sample_and_invoke_procedure() {
            auto& runner = std::get<I>(_runners);
            auto& sample = _cost_samples[I];

            if ((++sample.call_counter & COST_SAMPLE_PERIOD_MASK) != 0) {
                return std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
            }

            const auto items_before = sum_output_cumulative_write_count(runner.outputs);
            const auto t_before = std::chrono::steady_clock::now();
            auto result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            const auto t_after = std::chrono::steady_clock::now();

            if (result.is_ok()) {
                const auto items_after = sum_output_cumulative_write_count(runner.outputs);
                const double observed_ns = std::chrono::duration<double, std::nano>(t_after - t_before).count();
                const double observed_items = static_cast<double>(items_after - items_before);

                const double prev_ns = sample.ewma_ns_per_call.load(std::memory_order_relaxed);
                sample.ewma_ns_per_call.store(prev_ns + (observed_ns - prev_ns) / 8.0, std::memory_order_relaxed);

                const double prev_items = sample.ewma_items_per_call.load(std::memory_order_relaxed);
                sample.ewma_items_per_call.store(prev_items + (observed_items - prev_items) / 8.0, std::memory_order_relaxed);
            }

            return result;
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

            TaskPolicy::configure_thread_for_low_latency_sleep();

            if (config.collect_detailed_stats) {
                stats.name = runner.block->name();
            }

            std::chrono::high_resolution_clock::time_point t_start, t_last;
            size_t successful = 0, failed = 0;
            double total_dead_time_s = 0.0;

            if (config.collect_detailed_stats) {
                t_start = t_last = std::chrono::high_resolution_clock::now();
            }

            BackoffState backoff_state{};

            while (!_stop_flag.load(std::memory_order_relaxed)) {
                bool did_work_in_batch = false;
                bool batch_failed = false;

                for (size_t c = 0; c < config.max_calls_per_tick && !_stop_flag.load(std::memory_order_relaxed); ++c) {
                    std::chrono::duration<double> dt{};
                    if (config.collect_detailed_stats) {
                        auto t_now = std::chrono::high_resolution_clock::now();
                        dt = t_now - t_last;
                        t_last = t_now;
                    }

                    Result<Empty, Error> result = sample_and_invoke_procedure<I>();

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
                        }
                        batch_failed = true;
                        break;

                    } else {
                        if (config.collect_detailed_stats) {
                            successful++;
                        }
                        did_work_in_batch = true;
                    }
                }

                if (did_work_in_batch) {
                    handle_adaptive_sleep(I, true);
                    TaskPolicy::backoff_reset(backoff_state);
                    if (_pinned_worker_count > 0) {
                        wake_parked_workers(kNoWorker);
                    }
                } else if (batch_failed) {
                    double pending_us = stats.current_adaptive_sleep_us.load();
                    if (pending_us > 0.0) {
                        TaskPolicy::sleep_us(static_cast<size_t>(pending_us));
                    } else {
                        TaskPolicy::backoff(backoff_state);
                    }
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

        template<std::size_t I>
        void collect_regular_block_id_for_index(std::array<uint8_t, _N>& ids, size_t& count) {
            if (!std::get<I>(_runners).may_block) {
                ids[count++] = static_cast<uint8_t>(I);
            }
        }

        template<std::size_t... Is>
        void collect_regular_block_ids_impl(std::index_sequence<Is...>, std::array<uint8_t, _N>& ids, size_t& count) {
            (collect_regular_block_id_for_index<Is>(ids, count), ...);
        }

        template<std::size_t I>
        void launch_may_block_task_for_index(const FlowGraphConfig& config) {
            if (std::get<I>(_runners).may_block) {
                _tasks[_active_task_count++] = TaskPolicy::create_task([this, config]() {
                    run_block_at_index_thread_per_block<I>(config);
                });
            }
        }

        template<std::size_t... Is>
        void launch_may_block_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config) {
            (launch_may_block_task_for_index<Is>(config), ...);
        }

        template<std::size_t... Is>
        void launch_thread_per_block_task_dispatch_impl(std::index_sequence<Is...>, size_t index, const FlowGraphConfig& config) {
            (void)((index == Is ? (_tasks[_active_task_count++] = TaskPolicy::create_task([this, config]() {
                run_block_at_index_thread_per_block<Is>(config);
            }), true) : false) || ...);
        }


        void run_thread_per_block(const FlowGraphConfig& config) {
            _pinned_worker_count = 0;
            initialize_block_stats();
            launch_tasks_impl(std::make_index_sequence<_N>{}, config);
        }

        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag{false};
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        std::array<SchedulerCostSample, _N> _cost_samples;
        std::array<const BlockBase*, _N> _block_bases{};
        std::array<Edge, MaxEdges> _edges{};
        size_t _edge_count = 0;
        std::array<UnresolvedEdge, MaxEdges> _unresolved_edges{};
        size_t _unresolved_edge_count = 0;
        OnErrTerminateCallback _on_err_terminate_cb = nullptr;
        void* _on_err_terminate_context = nullptr;
        std::array<std::chrono::high_resolution_clock::time_point, _N> _block_start_times;
        size_t _active_task_count{0};  // Track actual created tasks to fix stop() hang

        struct alignas(platform::cache_line_size) WorkerParkState {
            std::atomic<uint32_t> sleep_epoch{0};
            std::atomic<bool> parked{false};
            std::atomic<uint64_t> park_events{0};
        };

        static constexpr size_t kNoWorker = (std::numeric_limits<size_t>::max)();
        static constexpr double CROSS_EDGE_PENALTY_NS = 200.0;
        static constexpr size_t CALIBRATION_DEADLINE_CHECK_PASSES = 256;

        std::array<WorkerParkState, DEFAULT_MAX_WORKERS> _park_states;
        Partition _partition;
        std::atomic<uint32_t> _partition_epoch{0};
        std::atomic<bool> _repartition_pending{false};
        std::atomic<size_t> _repartition_arrived{0};
        std::atomic<size_t> _repartition_count{0};
        std::atomic<size_t> _affinity_failures{0};
        std::chrono::steady_clock::time_point _calibration_deadline;
        size_t _pinned_worker_count = 0;

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
        class WorkerQueueScheduler {
            using block_index_t = uint8_t;

            static_assert(MaxBlocksParam >= 1, "Must support at least one block");
            static_assert(MaxWorkers >= 1, "Must support at least one worker");
            static_assert(MaxBlocksParam <= (std::numeric_limits<block_index_t>::max)(),
                          "MaxBlocksParam exceeds block_index_t capacity");

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

            static_assert(sizeof(WorkerQueue) <= platform::cache_line_size * 4,
                          "WorkerQueue is too large, consider reducing MaxBlocksParam");

            static constexpr size_t kNoOwner = (std::numeric_limits<size_t>::max)();

            std::array<WorkerQueue, MaxWorkers> queues;
            std::array<size_t, MaxBlocksParam> block_owner;

        public:
            void initialize(const block_index_t* block_ids, size_t block_id_count, size_t workers) {
                for (auto& q : queues) {
                    q.count = 0;
                    q.current = 0;
                }
                block_owner.fill(kNoOwner);

                const size_t per = (block_id_count + workers - 1) / workers;
                size_t idx = 0;
                for (size_t w = 0; w < workers && idx < block_id_count; ++w) {
                    auto& q = queues[w];
                    for (size_t k = 0; k < per && idx < block_id_count; ++k, ++idx) {
                        block_index_t bidx = block_ids[idx];
                        q.blocks[q.count++] = bidx;
                        block_owner[bidx] = w;
                    }
                }
            }

            void initialize_islands(const block_index_t* block_ids, const uint16_t* island_begin, size_t island_count) {
                for (auto& q : queues) {
                    q.count = 0;
                    q.current = 0;
                }
                block_owner.fill(kNoOwner);

                for (size_t w = 0; w < island_count && w < MaxWorkers; ++w) {
                    auto& q = queues[w];
                    for (uint16_t k = island_begin[w]; k < island_begin[w + 1]; ++k) {
                        block_index_t bidx = block_ids[k];
                        q.blocks[q.count++] = bidx;
                        block_owner[bidx] = w;
                    }
                }
            }

            bool get_next_block(size_t worker_id, size_t& block_idx_out) {
                return queues[worker_id].get_block(block_idx_out);
            }

            void reset_pass(size_t worker_id) {
                queues[worker_id].reset();
            }

            bool is_block_owner(size_t worker_id, size_t block_idx) const {
                return block_idx < block_owner.size() && block_owner[block_idx] == worker_id;
            }
        };
        
        
        WorkerQueueScheduler<MaxBlocks, DEFAULT_MAX_WORKERS> _worker_queues;

        void topo_sort_blocks(const uint8_t* ids, size_t count, std::array<uint8_t, _N>& out) const {
            std::array<uint16_t, _N> indegree{};
            std::array<uint8_t, _N> in_set{};
            std::array<uint8_t, _N> emitted{};

            for (size_t i = 0; i < count; ++i) in_set[ids[i]] = 1;
            for (size_t e = 0; e < _edge_count; ++e) {
                const Edge& edge = _edges[e];
                if (edge.producer == edge.consumer) continue;
                if (in_set[edge.producer] && in_set[edge.consumer]) ++indegree[edge.consumer];
            }

            size_t emitted_count = 0;
            bool progressed = true;
            while (progressed) {
                progressed = false;
                for (size_t i = 0; i < count; ++i) {
                    const uint8_t b = ids[i];
                    if (emitted[b] || indegree[b] != 0) continue;
                    emitted[b] = 1;
                    out[emitted_count++] = b;
                    progressed = true;
                    for (size_t e = 0; e < _edge_count; ++e) {
                        const Edge& edge = _edges[e];
                        if (edge.producer != b || edge.producer == edge.consumer) continue;
                        if (in_set[edge.consumer] && !emitted[edge.consumer]) --indegree[edge.consumer];
                    }
                }
            }
            for (size_t i = 0; i < count; ++i) {
                if (!emitted[ids[i]]) out[emitted_count++] = ids[i];
            }
        }

        void collect_block_weights(const std::array<uint8_t, _N>& order, size_t count,
                                   std::array<double, _N>& weights) const {
            std::array<double, _N> sorted{};
            size_t sampled = 0;

            for (size_t i = 0; i < count; ++i) {
                const auto& sample = _cost_samples[order[i]];
                const double ns = sample.ewma_ns_per_call.load(std::memory_order_relaxed);
                const double items = sample.ewma_items_per_call.load(std::memory_order_relaxed);
                weights[i] = ns > 0.0 ? ns / (std::max)(items, 1.0) : 0.0;
                if (weights[i] > 0.0) sorted[sampled++] = weights[i];
            }

            double median = 1.0;
            if (sampled > 0) {
                std::sort(sorted.begin(), sorted.begin() + sampled);
                median = sorted[sampled / 2];
            }
            for (size_t i = 0; i < count; ++i) {
                if (weights[i] <= 0.0) weights[i] = median;
            }
        }

        void count_cut_crossings(const std::array<uint8_t, _N>& order, size_t count,
                                 std::array<uint16_t, _N>& crossings) const {
            std::array<uint16_t, _N> position{};
            std::array<uint8_t, _N> in_order{};
            for (size_t i = 0; i < count; ++i) {
                position[order[i]] = static_cast<uint16_t>(i);
                in_order[order[i]] = 1;
            }

            for (size_t e = 0; e < _edge_count; ++e) {
                const Edge& edge = _edges[e];
                if (!in_order[edge.producer] || !in_order[edge.consumer]) continue;
                const uint16_t producer_pos = position[edge.producer];
                const uint16_t consumer_pos = position[edge.consumer];
                const uint16_t low = (std::min)(producer_pos, consumer_pos);
                const uint16_t high = (std::max)(producer_pos, consumer_pos);
                for (uint16_t cut = static_cast<uint16_t>(low + 1); cut <= high; ++cut) {
                    ++crossings[cut];
                }
            }
        }

        void build_partition(size_t worker_count, bool use_costs) {
            std::array<uint8_t, _N> regular_ids{};
            size_t regular_count = 0;
            collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, regular_ids, regular_count);

            std::array<uint8_t, _N> order{};
            topo_sort_blocks(regular_ids.data(), regular_count, order);

            const size_t islands = (std::max)(size_t{1}, (std::min)(worker_count, regular_count));
            _partition.block_ids = order;
            _partition.block_count = static_cast<uint16_t>(regular_count);
            _partition.island_count = static_cast<uint16_t>(islands);

            if (!use_costs) {
                size_t cursor = 0;
                for (size_t w = 0; w < islands; ++w) {
                    _partition.island_begin[w] = static_cast<uint16_t>(cursor);
                    cursor += regular_count / islands + (w < regular_count % islands ? 1 : 0);
                }
                _partition.island_begin[islands] = static_cast<uint16_t>(regular_count);
                return;
            }

            std::array<double, _N> weights{};
            collect_block_weights(order, regular_count, weights);

            std::array<double, _N + 1> prefix{};
            for (size_t i = 0; i < regular_count; ++i) prefix[i + 1] = prefix[i] + weights[i];

            std::array<uint16_t, _N> crossings{};
            count_cut_crossings(order, regular_count, crossings);

            std::array<double, _N + 1> prev_max{};
            std::array<double, _N + 1> prev_crossings{};
            std::array<double, _N + 1> island_max{};
            std::array<double, _N + 1> total_crossings{};
            std::array<std::array<uint16_t, _N + 1>, DEFAULT_MAX_WORKERS + 1> choice{};

            const double infinity = (std::numeric_limits<double>::max)();
            for (size_t end = 0; end <= regular_count; ++end) {
                prev_max[end] = (end == 0) ? 0.0 : infinity;
                prev_crossings[end] = 0.0;
            }

            for (size_t k = 1; k <= islands; ++k) {
                for (size_t end = 0; end <= regular_count; ++end) island_max[end] = infinity;
                for (size_t end = k; end <= regular_count - (islands - k); ++end) {
                    double best_objective = infinity;
                    for (size_t start = k - 1; start < end; ++start) {
                        if (prev_max[start] == infinity) continue;
                        const double candidate_max = (std::max)(prev_max[start], prefix[end] - prefix[start]);
                        const double candidate_crossings = prev_crossings[start] + (start > 0 ? crossings[start] : 0);
                        const double objective = candidate_max + CROSS_EDGE_PENALTY_NS * candidate_crossings;
                        if (objective < best_objective) {
                            best_objective = objective;
                            island_max[end] = candidate_max;
                            total_crossings[end] = candidate_crossings;
                            choice[k][end] = static_cast<uint16_t>(start);
                        }
                    }
                }
                prev_max = island_max;
                prev_crossings = total_crossings;
            }

            size_t end = regular_count;
            _partition.island_begin[islands] = static_cast<uint16_t>(regular_count);
            for (size_t k = islands; k >= 1; --k) {
                const uint16_t start = choice[k][end];
                _partition.island_begin[k - 1] = start;
                end = start;
            }
        }

        void apply_partition() {
            _worker_queues.initialize_islands(_partition.block_ids.data(),
                                              _partition.island_begin.data(),
                                              _partition.island_count);
        }

        void wake_parked_workers(size_t self_id) {
            for (size_t w = 0; w < _pinned_worker_count; ++w) {
                if (w == self_id) continue;
                WorkerParkState& state = _park_states[w];
                if (!state.parked.load(std::memory_order_relaxed)) continue;
                if (!state.parked.exchange(false, std::memory_order_acq_rel)) continue;
                state.sleep_epoch.fetch_add(1, std::memory_order_release);
                TaskPolicy::unpark(state.sleep_epoch);
            }
        }

        void unpark_everyone() {
            for (auto& state : _park_states) {
                state.parked.store(false, std::memory_order_relaxed);
                state.sleep_epoch.fetch_add(1, std::memory_order_release);
                TaskPolicy::unpark(state.sleep_epoch);
            }
            _partition_epoch.fetch_add(1, std::memory_order_release);
            TaskPolicy::unpark(_partition_epoch);
        }

        void run_pinned_islands(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);

            initialize_block_stats();

            if (config.collect_detailed_stats) {
                auto start_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    _block_start_times[i] = start_time;
                }
            }

            std::array<uint8_t, _N> regular_ids{};
            size_t regular_count = 0;
            collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, regular_ids, regular_count);

            for (auto& state : _park_states) {
                state.sleep_epoch.store(0, std::memory_order_relaxed);
                state.parked.store(false, std::memory_order_relaxed);
                state.park_events.store(0, std::memory_order_relaxed);
            }
            _partition_epoch.store(0, std::memory_order_relaxed);
            _repartition_pending.store(false, std::memory_order_relaxed);
            _repartition_arrived.store(0, std::memory_order_relaxed);
            _repartition_count.store(0, std::memory_order_relaxed);
            _affinity_failures.store(0, std::memory_order_relaxed);
            _calibration_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(config.calibration_ms);

            const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
            _pinned_worker_count = regular_count == 0
                ? 0
                : (std::max)(size_t{1}, (std::min)(config.num_workers, max_worker_count));

            _active_task_count = 0;
            launch_may_block_tasks_impl(std::make_index_sequence<_N>{}, config);

            if (regular_count == 0) return;

            build_partition(_pinned_worker_count, false);
            apply_partition();

            for (size_t worker_id = 0; worker_id < _pinned_worker_count; ++worker_id) {
                _tasks[_active_task_count] = TaskPolicy::create_task([this, worker_id, config]() {
                    run_pinned_islands_worker(worker_id, config);
                });
                _active_task_count++;
            }
        }

        void run_fixed_thread_pool(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            _pinned_worker_count = 0;

            assert(config.num_workers >= 2 && "FixedThreadPool requires at least 2 workers. Use ThreadPerBlock scheduler for single-threaded execution.");

            initialize_block_stats();

            if (config.collect_detailed_stats) {
                auto start_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    _block_start_times[i] = start_time;
                }
            }

            std::array<uint8_t, _N> regular_ids{};
            size_t regular_count = 0;
            collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, regular_ids, regular_count);

            _active_task_count = 0;
            launch_may_block_tasks_impl(std::make_index_sequence<_N>{}, config);

            if (regular_count == 0) return;

            const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
            const size_t effective_worker_count = (std::max)(size_t{1}, (std::min)(config.num_workers, max_worker_count));

            if (effective_worker_count >= regular_count) {
                for (size_t idx = 0; idx < regular_count; ++idx) {
                    launch_thread_per_block_task_dispatch_impl(std::make_index_sequence<_N>{}, regular_ids[idx], config);
                }
            } else {
                _worker_queues.initialize(regular_ids.data(), regular_count, effective_worker_count);

                for (size_t worker_id = 0; worker_id < effective_worker_count; ++worker_id) {
                    _tasks[_active_task_count] = TaskPolicy::create_task([this, worker_id, config]() {
                        run_fixed_thread_pool_worker(worker_id, config);
                    });
                    _active_task_count++;
                }
            }
        }
        
    public:  // Making run_fixed_thread_pool_worker public for lambda access (see comment above)
        void run_pinned_islands_worker(size_t worker_id, const FlowGraphConfig& config) {
            TaskPolicy::configure_thread_for_low_latency_sleep();
            if (!TaskPolicy::pin_to_core(config.cpu_id_offset + worker_id)) {
                _affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }

            WorkerParkState& park_state = _park_states[worker_id];
            BackoffState backoff_state{};
            size_t zero_progress_passes = 0;
            size_t passes_since_deadline_check = 0;
            bool park_armed = false;
            uint32_t observed_sleep_epoch = 0;

            while (!_stop_flag.load(std::memory_order_relaxed)) {
                _worker_queues.reset_pass(worker_id);
                bool did_work_in_pass = false;
                size_t block_idx;

                while (_worker_queues.get_next_block(worker_id, block_idx)) {
                    if (_stop_flag.load(std::memory_order_relaxed)) break;

                    auto t_before = get_time_if_needed(config.collect_detailed_stats);

                    bool block_did_work = execute_block_at_index(block_idx, config);

                    if (!block_did_work && config.collect_detailed_stats) {
                        auto t_after = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<double> dt = t_after - t_before;
                        _stats[block_idx].total_dead_time_s += dt.count();
                    }

                    did_work_in_pass = did_work_in_pass || block_did_work;
                }

                if (worker_id == 0 && !_repartition_pending.load(std::memory_order_relaxed) &&
                    _repartition_count.load(std::memory_order_relaxed) == 0 &&
                    ++passes_since_deadline_check >= CALIBRATION_DEADLINE_CHECK_PASSES) {
                    passes_since_deadline_check = 0;
                    if (std::chrono::steady_clock::now() >= _calibration_deadline) {
                        _repartition_pending.store(true, std::memory_order_release);
                        wake_parked_workers(worker_id);
                    }
                }

                if (_repartition_pending.load(std::memory_order_relaxed)) {
                    if (park_armed) {
                        park_armed = false;
                        park_state.parked.store(false, std::memory_order_relaxed);
                    }
                    repartition_barrier(worker_id);
                    zero_progress_passes = 0;
                    TaskPolicy::backoff_reset(backoff_state);
                    continue;
                }

                if (did_work_in_pass) {
                    zero_progress_passes = 0;
                    park_armed = false;
                    if (park_state.parked.load(std::memory_order_relaxed)) {
                        park_state.parked.store(false, std::memory_order_relaxed);
                    }
                    TaskPolicy::backoff_reset(backoff_state);
                    wake_parked_workers(worker_id);
                    continue;
                }

                ++zero_progress_passes;

                if (zero_progress_passes <= config.park_after_zero_passes) {
                    TaskPolicy::backoff(backoff_state);
                } else if (!park_armed) {
                    park_armed = true;
                    observed_sleep_epoch = park_state.sleep_epoch.load(std::memory_order_acquire);
                    park_state.parked.store(true, std::memory_order_release);
                } else {
                    park_state.park_events.fetch_add(1, std::memory_order_relaxed);
                    TaskPolicy::park(park_state.sleep_epoch, observed_sleep_epoch);
                    park_armed = false;
                    park_state.parked.store(false, std::memory_order_relaxed);
                }
            }

            park_state.parked.store(false, std::memory_order_relaxed);

            if (config.collect_detailed_stats) {
                auto end_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    if (_worker_queues.is_block_owner(worker_id, i)) {
                        std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                        _stats[i].total_runtime_s = total_runtime.count();
                        _stats[i].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[i].current_adaptive_sleep_us.load() : 0.0;
                    }
                }
            }
        }

        void repartition_barrier(size_t worker_id) {
            const uint32_t epoch_before = _partition_epoch.load(std::memory_order_acquire);
            _repartition_arrived.fetch_add(1, std::memory_order_acq_rel);

            if (worker_id != 0) {
                while (_partition_epoch.load(std::memory_order_acquire) == epoch_before &&
                       !_stop_flag.load(std::memory_order_relaxed)) {
                    TaskPolicy::park(_partition_epoch, epoch_before);
                }
                return;
            }

            while (_repartition_arrived.load(std::memory_order_acquire) < _pinned_worker_count &&
                   !_stop_flag.load(std::memory_order_relaxed)) {
                wake_parked_workers(worker_id);
                TaskPolicy::yield();
            }

            build_partition(_pinned_worker_count, true);
            apply_partition();
            _repartition_pending.store(false, std::memory_order_relaxed);
            _repartition_count.fetch_add(1, std::memory_order_release);
            _partition_epoch.fetch_add(1, std::memory_order_release);
            TaskPolicy::unpark(_partition_epoch);
        }

        void run_fixed_thread_pool_worker(size_t worker_id, const FlowGraphConfig& config) {
            TaskPolicy::configure_thread_for_low_latency_sleep();
            if (config.pin_workers) {
                TaskPolicy::pin_to_core(worker_id);
            }

            BackoffState backoff_state{};

            while (!_stop_flag.load(std::memory_order_relaxed)) {
                _worker_queues.reset_pass(worker_id);
                bool did_work_in_pass = false;
                size_t block_idx;

                while (_worker_queues.get_next_block(worker_id, block_idx)) {
                    if (_stop_flag.load(std::memory_order_relaxed)) break;

                    auto t_before = get_time_if_needed(config.collect_detailed_stats);

                    bool block_did_work = execute_block_at_index(block_idx, config);

                    if (!block_did_work && config.collect_detailed_stats) {
                        auto t_after = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<double> dt = t_after - t_before;
                        _stats[block_idx].total_dead_time_s += dt.count();
                    }

                    did_work_in_pass = did_work_in_pass || block_did_work;
                }

                if (did_work_in_pass) {
                    TaskPolicy::backoff_reset(backoff_state);
                } else {
                    double pending_us = min_pending_backoff_us_for_worker(worker_id);
                    if (pending_us > 0.0) {
                        TaskPolicy::sleep_us(static_cast<size_t>(pending_us));
                    } else {
                        TaskPolicy::backoff(backoff_state);
                    }
                }
            }

            if (config.collect_detailed_stats) {
                auto end_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    if (_worker_queues.is_block_owner(worker_id, i)) {
                        std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                        _stats[i].total_runtime_s = total_runtime.count();
                        _stats[i].final_adaptive_sleep_us = config.adaptive_sleep ? _stats[i].current_adaptive_sleep_us.load() : 0.0;
                    }
                }
            }
        }
        
    private:  // Return to private section for internal implementation details
        double min_pending_backoff_us_for_worker(size_t worker_id) const {
            double min_us = 0.0;
            for (size_t i = 0; i < _N; ++i) {
                if (!_worker_queues.is_block_owner(worker_id, i)) continue;
                double pending = _stats[i].current_adaptive_sleep_us.load();
                if (pending <= 0.0) continue;
                if (min_us <= 0.0 || pending < min_us) {
                    min_us = pending;
                }
            }
            return min_us;
        }

        template<size_t I>
        bool execute_block_at_index_helper(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");

            auto& stats = _stats[I];

            bool did_work = false;

            for (size_t c = 0; c < config.max_calls_per_tick; ++c) {
                auto result = sample_and_invoke_procedure<I>();

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
                        break;
                    }

                    if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace ||
                        err == Error::NotEnoughSpaceOrSamples) {
                        handle_adaptive_sleep(I, false);
                    }
                    break;
                } else {
                    if (config.collect_detailed_stats) {
                        stats.successful_procedures++;
                    }
                    did_work = true;
                }
            }

            if (did_work) {
                handle_adaptive_sleep(I, true);
            }

            return did_work;
        }

        bool execute_block_at_index(size_t index, const FlowGraphConfig& config) {
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        
    };

    constexpr float PI = 3.14159265358979323846f;

} // namespace cler