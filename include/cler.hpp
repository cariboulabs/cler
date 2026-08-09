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
#include <atomic>
#include <limits> // for std::numeric_limits
#include <type_traits>
#include <cstdint>
#include <cstring>

namespace cler {

    constexpr size_t DOUBLY_MAPPED_MIN_SIZE = dro::details::DOUBLY_MAPPED_MIN_SIZE;

    #ifndef CLER_DEFAULT_MAX_WORKERS
    #define CLER_DEFAULT_MAX_WORKERS (8)  // Conservative default for embedded systems
    #endif
    constexpr size_t DEFAULT_MAX_WORKERS = CLER_DEFAULT_MAX_WORKERS;

    enum class Error {
        OK,

        Unknown,
        NotEnoughSamples,
        NotEnoughSpace,
        NotEnoughSpaceOrSamples, // for lazyness
        ProcedureError,
        BadData,
        TERMINATE_FLOWGRAPH,
        TERM_InvalidChannelIndex,
        TERM_ProcedureError,
        TERM_IOError,
        TERM_EOFReached,
    };
    
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
        virtual std::size_t consumer_thread_cumulative_read_count() const = 0;
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
        std::size_t consumer_thread_cumulative_read_count() const override {
            return _queue.consumer_thread_cumulative_read_count();
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

    private:
        EmbeddableString<64> _name;
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
    
    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 4;
        bool collect_detailed_stats = false;
        size_t max_calls_per_tick = 4;
        bool pin_workers = false;
        size_t calibration_ms = 500;
        size_t repartition_check_ms = 5000;
        size_t cpu_id_offset = 0;
        size_t park_after_zero_passes = 4;
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        static constexpr std::size_t MaxBlocks = sizeof...(BlockRunners);  // Clean compile-time constant
        static_assert(_N > 0, "FlowGraph must have at least one block");
        static constexpr std::size_t MaxSupportedBlocks =
            (std::min)(static_cast<std::size_t>((std::numeric_limits<uint8_t>::max)()),
                       platform::cache_line_size * 4 - 2 * sizeof(uint32_t));
        static_assert(_N <= MaxSupportedBlocks,
                      "FlowGraph has more blocks than the scheduler's worker queue can index; "
                      "the bound is min(255, cache_line_size * 4 - 8) and is platform-dependent");
        static constexpr std::size_t MaxEdges = (runner_output_count<BlockRunners>::value + ... + 0);
        using OnErrTerminateCallback = void (*)(void* context);

        struct UnresolvedEdge {
            uint8_t producer;
            const void* address;
        };

        struct InputCounter {
            const void* channel;
            std::size_t (*read_count)(const void*);
            uint8_t consumer;
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
            auto start_time = std::chrono::high_resolution_clock::now();
            run(config);
            
            static constexpr int64_t PRECISE_TIMING_THRESHOLD_US = 100000;  // 100ms
            static constexpr int64_t PRECISE_TIMING_BUFFER_US = 50000;      // 50ms
            
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            if (total_us > PRECISE_TIMING_THRESHOLD_US) { // More than 100ms
                TaskPolicy::sleep_us(total_us - PRECISE_TIMING_BUFFER_US);
            }
            
            while (std::chrono::high_resolution_clock::now() - start_time < duration) {
                TaskPolicy::relax();
            }
            
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

        const FlowGraphConfig& config() const { return _config; }
        const std::array<BlockExecutionStats, _N>& stats() const { return _stats; }

        std::array<BlockCost, _N> block_costs() const {
            std::array<BlockCost, _N> out{};
            for (size_t i = 0; i < _N; ++i) {
                out[i].ewma_ns_per_call = bits_to_double(_cost_samples[i].ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                out[i].ewma_items_per_call = bits_to_double(_cost_samples[i].ewma_items_per_call_bits.load(std::memory_order_relaxed));
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

        template<typename Ch>
        void resolve_and_add_edge(uint8_t producer, Ch* channel, const std::array<BlockSpan, _N>& spans) {
            const void* address = static_cast<const void*>(channel);
            for (size_t k = 0; k < _N; ++k) {
                if (address >= spans[k].begin && address < spans[k].end) {
                    _edges[_edge_count++] = Edge{producer, static_cast<uint8_t>(k)};
                    _input_counters[_input_counter_count++] = InputCounter{
                        address,
                        [](const void* p) -> std::size_t {
                            return static_cast<const Ch*>(p)->consumer_thread_cumulative_read_count();
                        },
                        static_cast<uint8_t>(k)
                    };
                    return;
                }
            }
            _unresolved_edges[_unresolved_edge_count++] = UnresolvedEdge{producer, address};
        }

        template<std::size_t I>
        void collect_edges_for_index(const std::array<BlockSpan, _N>& spans) {
            auto& runner = std::get<I>(_runners);
            std::apply([&](auto*... outs) {
                (resolve_and_add_edge(static_cast<uint8_t>(I), outs, spans), ...);
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

        static constexpr size_t COST_SAMPLE_PERIOD_CALLS = 61;

        static uint64_t double_to_bits(double value) {
            uint64_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        static double bits_to_double(uint64_t bits) {
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        struct alignas(platform::cache_line_size) SchedulerCostSample {
            size_t calls_until_sample = COST_SAMPLE_PERIOD_CALLS;
            std::atomic<uint64_t> ewma_ns_per_call_bits{0};
            std::atomic<uint64_t> ewma_items_per_call_bits{0};
        };

        template<typename... Channels>
        static std::size_t sum_output_cumulative_write_count(const std::tuple<Channels*...>& outputs) {
            return std::apply([](auto*... outs) {
                return (std::size_t{0} + ... + outs->producer_thread_cumulative_write_count());
            }, outputs);
        }

        bool snapshot_input_reads(uint8_t block_id, std::array<std::size_t, MaxEdges>& snap) const {
            bool found = false;
            for (size_t i = 0; i < _input_counter_count; ++i) {
                const auto& ic = _input_counters[i];
                if (ic.consumer != block_id) continue;
                found = true;
                snap[i] = ic.read_count(ic.channel);
            }
            return found;
        }

        std::size_t max_input_read_delta(uint8_t block_id,
                                         const std::array<std::size_t, MaxEdges>& snap) const {
            std::size_t max_delta = 0;
            for (size_t i = 0; i < _input_counter_count; ++i) {
                const auto& ic = _input_counters[i];
                if (ic.consumer != block_id) continue;
                const std::size_t delta = ic.read_count(ic.channel) - snap[i];
                if (delta > max_delta) max_delta = delta;
            }
            return max_delta;
        }

        template<std::size_t I>
        Result<Empty, Error> sample_and_invoke_procedure() {
            auto& runner = std::get<I>(_runners);
            auto& sample = _cost_samples[I];

            if (!_cost_sampling_enabled || --sample.calls_until_sample != 0) {
                return std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
            }
            sample.calls_until_sample = COST_SAMPLE_PERIOD_CALLS;

            std::array<std::size_t, MaxEdges> input_snapshot{};
            const bool has_inputs = snapshot_input_reads(static_cast<uint8_t>(I), input_snapshot);
            const std::size_t writes_before = sum_output_cumulative_write_count(runner.outputs);

            const auto t_before = std::chrono::steady_clock::now();
            auto result = std::apply([&](auto*... outs) {
                return runner.block->procedure(outs...);
            }, runner.outputs);
            const auto t_after = std::chrono::steady_clock::now();

            if (result.is_ok()) {
                const double observed_ns = std::chrono::duration<double, std::nano>(t_after - t_before).count();
                const std::size_t write_delta =
                    sum_output_cumulative_write_count(runner.outputs) - writes_before;
                const std::size_t read_delta = has_inputs
                    ? max_input_read_delta(static_cast<uint8_t>(I), input_snapshot)
                    : std::size_t{0};
                const double observed_items =
                    static_cast<double>(read_delta > 0 ? read_delta : write_delta);

                const double prev_ns = bits_to_double(sample.ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                sample.ewma_ns_per_call_bits.store(double_to_bits(prev_ns + (observed_ns - prev_ns) / 8.0),
                                                   std::memory_order_relaxed);

                const double prev_items = bits_to_double(sample.ewma_items_per_call_bits.load(std::memory_order_relaxed));
                sample.ewma_items_per_call_bits.store(double_to_bits(prev_items + (observed_items - prev_items) / 8.0),
                                                      std::memory_order_relaxed);
            }

            return result;
        }

    public:
        
        struct NoProgressNotification {
            void operator()() const {}
        };

        struct WakePinnedWorkers {
            FlowGraph* graph;
            void operator()() const { graph->wake_parked_workers(kNoWorker); }
        };

        template<std::size_t I, typename ProgressNotifier>
        void run_block_at_index_thread_per_block(const FlowGraphConfig& config,
                                                 ProgressNotifier notify_progress) {
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
                    TaskPolicy::backoff_reset(backoff_state);
                    notify_progress();
                } else if (batch_failed) {
                    TaskPolicy::backoff(backoff_state);
                }
            }

            if (config.collect_detailed_stats) {
                auto t_end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> total_runtime_s = t_end - t_start;

                stats.successful_procedures = successful;
                stats.failed_procedures = failed;
                stats.total_dead_time_s = total_dead_time_s;
                stats.total_runtime_s = total_runtime_s.count();
            }
        }
        
    private:  // Return to private section for internal implementation
        template<std::size_t... Is>
        void launch_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            ((_tasks[Is] = TaskPolicy::create_task([this, config]() {
                run_block_at_index_thread_per_block<Is>(config, NoProgressNotification{});
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

        template<std::size_t I, typename ProgressNotifier>
        void launch_may_block_task_for_index(const FlowGraphConfig& config, ProgressNotifier notify_progress) {
            if (std::get<I>(_runners).may_block) {
                _tasks[_active_task_count++] = TaskPolicy::create_task([this, config, notify_progress]() {
                    run_block_at_index_thread_per_block<I>(config, notify_progress);
                });
            }
        }

        template<typename ProgressNotifier, std::size_t... Is>
        void launch_may_block_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config,
                                         ProgressNotifier notify_progress) {
            (launch_may_block_task_for_index<Is>(config, notify_progress), ...);
        }

        template<typename ProgressNotifier, std::size_t... Is>
        void launch_thread_per_block_task_dispatch_impl(std::index_sequence<Is...>, size_t index,
                                                        const FlowGraphConfig& config,
                                                        ProgressNotifier notify_progress) {
            (void)((index == Is ? (_tasks[_active_task_count++] = TaskPolicy::create_task([this, config, notify_progress]() {
                run_block_at_index_thread_per_block<Is>(config, notify_progress);
            }), true) : false) || ...);
        }


        void run_thread_per_block(const FlowGraphConfig& config) {
            reset_run_state();
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
        std::array<InputCounter, MaxEdges> _input_counters{};
        size_t _input_counter_count = 0;
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
        static constexpr uint32_t kNoRepartitionRequest = (std::numeric_limits<uint32_t>::max)();
        static constexpr double CROSS_EDGE_PENALTY_NS = 200.0;
        static constexpr size_t CALIBRATION_DEADLINE_CHECK_PASSES = 256;
        static constexpr double REPARTITION_IMPROVEMENT_RATIO = 0.8;

        static constexpr uint64_t barrier_word(uint32_t generation, uint32_t arrived) {
            return (static_cast<uint64_t>(generation) << 32) | arrived;
        }
        static constexpr uint32_t barrier_generation(uint64_t word) { return static_cast<uint32_t>(word >> 32); }
        static constexpr uint32_t barrier_arrived(uint64_t word) { return static_cast<uint32_t>(word); }

        std::array<WorkerParkState, DEFAULT_MAX_WORKERS> _park_states;
        Partition _partition;
        std::atomic<uint32_t> _partition_epoch{0};
        std::atomic<uint32_t> _repartition_request{kNoRepartitionRequest};
        std::atomic<uint64_t> _repartition_barrier{0};
        std::atomic<size_t> _repartition_count{0};
        std::atomic<size_t> _affinity_failures{0};
        std::chrono::steady_clock::time_point _calibration_deadline;
        std::chrono::steady_clock::time_point _next_repartition_check;
        size_t _pinned_worker_count = 0;
        bool _cost_sampling_enabled = false;

        void initialize_block_stats() {
            if (_config.collect_detailed_stats) {
                init_stats_impl(std::make_index_sequence<_N>{});
            }
        }
        
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
                const uint8_t id = order[i];
                const auto& sample = _cost_samples[id];
                const double ns = bits_to_double(sample.ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                const double items = bits_to_double(sample.ewma_items_per_call_bits.load(std::memory_order_relaxed));
                weights[id] = ns > 0.0 ? ns / (std::max)(items, 1.0) : 0.0;
                if (weights[id] > 0.0) sorted[sampled++] = weights[id];
            }

            double median = 1.0;
            if (sampled > 0) {
                std::sort(sorted.begin(), sorted.begin() + sampled);
                median = sorted[sampled / 2];
            }
            for (size_t i = 0; i < count; ++i) {
                if (weights[order[i]] <= 0.0) weights[order[i]] = median;
            }
        }

        double max_island_weight(const Partition& partition, const std::array<double, _N>& weights) const {
            double worst = 0.0;
            for (size_t w = 0; w < partition.island_count; ++w) {
                double island = 0.0;
                for (uint16_t k = partition.island_begin[w]; k < partition.island_begin[w + 1]; ++k) {
                    island += weights[partition.block_ids[k]];
                }
                worst = (std::max)(worst, island);
            }
            return worst;
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

        void build_partition(size_t worker_count, bool use_costs, Partition& out) {
            std::array<uint8_t, _N> regular_ids{};
            size_t regular_count = 0;
            collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, regular_ids, regular_count);

            const bool topology_is_complete = (_unresolved_edge_count == 0);

            std::array<uint8_t, _N> order{};
            if (topology_is_complete) {
                topo_sort_blocks(regular_ids.data(), regular_count, order);
            } else {
                order = regular_ids;
            }

            const size_t islands = (std::max)(size_t{1}, (std::min)(worker_count, regular_count));
            out.block_ids = order;
            out.block_count = static_cast<uint16_t>(regular_count);
            out.island_count = static_cast<uint16_t>(islands);

            if (!use_costs || !topology_is_complete) {
                size_t cursor = 0;
                for (size_t w = 0; w < islands; ++w) {
                    out.island_begin[w] = static_cast<uint16_t>(cursor);
                    cursor += regular_count / islands + (w < regular_count % islands ? 1 : 0);
                }
                out.island_begin[islands] = static_cast<uint16_t>(regular_count);
                return;
            }

            std::array<double, _N> weights{};
            collect_block_weights(order, regular_count, weights);

            std::array<double, _N + 1> prefix{};
            for (size_t i = 0; i < regular_count; ++i) prefix[i + 1] = prefix[i] + weights[order[i]];

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
            out.island_begin[islands] = static_cast<uint16_t>(regular_count);
            for (size_t k = islands; k >= 1; --k) {
                const uint16_t start = choice[k][end];
                out.island_begin[k - 1] = start;
                end = start;
            }
        }

        bool leader_should_repartition(const FlowGraphConfig& config) {
            const bool calibrated = _repartition_count.load(std::memory_order_relaxed) != 0;
            if (calibrated && config.repartition_check_ms == 0) return false;

            const auto now = std::chrono::steady_clock::now();
            if (!calibrated) return now >= _calibration_deadline;
            if (now < _next_repartition_check) return false;
            _next_repartition_check = now + std::chrono::milliseconds(config.repartition_check_ms);

            std::array<double, _N> weights{};
            collect_block_weights(_partition.block_ids, _partition.block_count, weights);

            Partition candidate;
            build_partition(_pinned_worker_count, true, candidate);

            return max_island_weight(candidate, weights) <
                   REPARTITION_IMPROVEMENT_RATIO * max_island_weight(_partition, weights);
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

        void reset_run_state() {
            for (auto& stats : _stats) stats = BlockExecutionStats{};
            for (auto& sample : _cost_samples) {
                sample.calls_until_sample = COST_SAMPLE_PERIOD_CALLS;
                sample.ewma_ns_per_call_bits.store(0, std::memory_order_relaxed);
                sample.ewma_items_per_call_bits.store(0, std::memory_order_relaxed);
            }
            for (auto& state : _park_states) {
                state.sleep_epoch.store(0, std::memory_order_relaxed);
                state.parked.store(false, std::memory_order_relaxed);
                state.park_events.store(0, std::memory_order_relaxed);
            }
            _partition = Partition{};
            _partition_epoch.store(0, std::memory_order_relaxed);
            _repartition_request.store(kNoRepartitionRequest, std::memory_order_relaxed);
            _repartition_barrier.store(barrier_word(0, 0), std::memory_order_relaxed);
            _repartition_count.store(0, std::memory_order_relaxed);
            _affinity_failures.store(0, std::memory_order_relaxed);
            _active_task_count = 0;
            _pinned_worker_count = 0;
            _cost_sampling_enabled = false;
        }

        void run_pinned_islands(const FlowGraphConfig& config) {
            _stop_flag.store(false, std::memory_order_release);
            reset_run_state();
            _cost_sampling_enabled = true;

            initialize_block_stats();

            if (config.collect_detailed_stats) {
                auto start_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    _block_start_times[i] = start_time;
                }
            }

            for (size_t i = 0; i < _unresolved_edge_count; ++i) {
                TaskPolicy::warn_unresolved_edge(block_name(_unresolved_edges[i].producer),
                                                 _unresolved_edges[i].address);
            }

            std::array<uint8_t, _N> regular_ids{};
            size_t regular_count = 0;
            collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, regular_ids, regular_count);

            _calibration_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(config.calibration_ms);
            _next_repartition_check = _calibration_deadline;

            const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
            _pinned_worker_count = regular_count == 0
                ? 0
                : (std::max)(size_t{1}, (std::min)(config.num_workers, max_worker_count));

            launch_may_block_tasks_impl(std::make_index_sequence<_N>{}, config,
                                        WakePinnedWorkers{this});

            if (regular_count == 0) return;

            build_partition(_pinned_worker_count, false, _partition);
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
            reset_run_state();

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

            launch_may_block_tasks_impl(std::make_index_sequence<_N>{}, config,
                                        NoProgressNotification{});

            if (regular_count == 0) return;

            const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
            const size_t effective_worker_count = (std::max)(size_t{1}, (std::min)(config.num_workers, max_worker_count));

            if (effective_worker_count >= regular_count) {
                for (size_t idx = 0; idx < regular_count; ++idx) {
                    launch_thread_per_block_task_dispatch_impl(std::make_index_sequence<_N>{}, regular_ids[idx], config,
                                                               NoProgressNotification{});
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
            bool pass_follows_park = false;
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

                const bool deadline_check_due =
                    ++passes_since_deadline_check >= CALIBRATION_DEADLINE_CHECK_PASSES || pass_follows_park;
                pass_follows_park = false;

                if (worker_id == 0 && deadline_check_due &&
                    _repartition_request.load(std::memory_order_relaxed) == kNoRepartitionRequest) {
                    passes_since_deadline_check = 0;
                    if (leader_should_repartition(config)) {
                        _repartition_request.store(_partition_epoch.load(std::memory_order_relaxed),
                                                   std::memory_order_release);
                        wake_parked_workers(worker_id);
                    }
                }

                const uint32_t requested_generation = _repartition_request.load(std::memory_order_relaxed);
                if (requested_generation != kNoRepartitionRequest) {
                    if (park_armed) {
                        park_armed = false;
                        park_state.parked.store(false, std::memory_order_relaxed);
                    }
                    repartition_barrier(worker_id, requested_generation);
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
                    pass_follows_park = true;
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
                    }
                }
            }
        }

        bool arrive_at_repartition_barrier(uint32_t generation) {
            uint64_t word = _repartition_barrier.load(std::memory_order_acquire);
            while (barrier_generation(word) == generation) {
                if (_repartition_barrier.compare_exchange_weak(word, word + 1,
                                                               std::memory_order_acq_rel,
                                                               std::memory_order_acquire)) {
                    return true;
                }
            }
            return false;
        }

        void repartition_barrier(size_t worker_id, uint32_t generation) {
            if (!arrive_at_repartition_barrier(generation)) return;

            if (worker_id != 0) {
                while (_partition_epoch.load(std::memory_order_acquire) == generation &&
                       !_stop_flag.load(std::memory_order_relaxed)) {
                    TaskPolicy::park(_partition_epoch, generation);
                }
                return;
            }

            while (barrier_arrived(_repartition_barrier.load(std::memory_order_acquire)) < _pinned_worker_count &&
                   !_stop_flag.load(std::memory_order_relaxed)) {
                wake_parked_workers(worker_id);
                TaskPolicy::yield();
            }

            if (!_stop_flag.load(std::memory_order_relaxed)) {
                build_partition(_pinned_worker_count, true, _partition);
                apply_partition();
                _next_repartition_check = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(_config.repartition_check_ms);
                _repartition_count.fetch_add(1, std::memory_order_relaxed);
            }

            _repartition_barrier.store(barrier_word(generation + 1, 0), std::memory_order_release);
            _repartition_request.store(kNoRepartitionRequest, std::memory_order_release);
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
                    TaskPolicy::backoff(backoff_state);
                }
            }

            if (config.collect_detailed_stats) {
                auto end_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    if (_worker_queues.is_block_owner(worker_id, i)) {
                        std::chrono::duration<double> total_runtime = end_time - _block_start_times[i];
                        _stats[i].total_runtime_s = total_runtime.count();
                    }
                }
            }
        }

    private:  // Return to private section for internal implementation details
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

                    break;
                } else {
                    if (config.collect_detailed_stats) {
                        stats.successful_procedures++;
                    }
                    did_work = true;
                }
            }

            return did_work;
        }

        bool execute_block_at_index(size_t index, const FlowGraphConfig& config) {
            return execute_block_dispatch_impl(std::make_index_sequence<_N>{}, index, config);
        }
        
    };

    constexpr float PI = 3.14159265358979323846f;

} // namespace cler