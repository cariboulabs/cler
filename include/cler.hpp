#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include "cler_embeddable_string.hpp"
#include "cler_platform.hpp"
#include "task_policies/cler_task_policy_base.hpp"
#include "schedulers/cler_scheduler_config.hpp"
#include "schedulers/detail/cler_topology.hpp"
#include "schedulers/detail/cler_park.hpp"
#include "schedulers/detail/cler_barrier.hpp"
#include "schedulers/cler_thread_per_block.hpp"
#include "schedulers/cler_fixed_thread_pool.hpp"
#include "schedulers/cler_pinned_islands.hpp"
#include "schedulers/detail/cler_partition.hpp"
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
        virtual void reset() = 0;
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
        void reset() override { _queue.reset(); }

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

    struct IslandCheck {
        enum class Code : uint8_t {
            Ok, BothFormsGiven, NoIslands, TooManyIslands, EmptyIsland,
            NotInGraph, NotSchedulable, Duplicate, Missing, NotTopological
        };

        Code             code   = Code::Ok;
        size_t           island = 0;
        const BlockBase* block  = nullptr;

        explicit operator bool() const { return code == Code::Ok; }

        const char* message() const {
            switch (code) {
                case Code::Ok:             return "ok";
                case Code::BothFormsGiven: return "set pinned_islands.islands or .island_names, not both";
                case Code::NoIslands:      return "pinned_islands.island_count is zero";
                case Code::TooManyIslands: return "more islands than workers";
                case Code::EmptyIsland:    return "an island is empty, which would idle a worker";
                case Code::NotInGraph:     return "a listed block is not in this flowgraph";
                case Code::NotSchedulable: return "a listed block declares may_block, it gets its own thread";
                case Code::Duplicate:      return "a block is listed on more than one island";
                case Code::Missing:        return "a schedulable block is on no island";
                case Code::NotTopological: return "within one island a block is listed before the block it consumes from";
            }
            return "unknown island error";
        }
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

    template<typename Block, typename = void>
    struct block_declares_is_gui : std::false_type {};
    template<typename Block>
    struct block_declares_is_gui<Block, std::enable_if_t<Block::is_gui>> : std::true_type {};
    template<typename Block>
    constexpr bool block_declares_is_gui_v =
        block_declares_is_gui<std::remove_cv_t<std::remove_reference_t<Block>>>::value;

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

        using Partition = sched::Partition<_N, DEFAULT_MAX_WORKERS>;

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

        class SchedulerHost {
        public:
            using TaskPolicyType = TaskPolicy;
            using PartitionType = Partition;
            static constexpr size_t block_count = _N;

            explicit SchedulerHost(FlowGraph& graph) : _graph(&graph) {}

            bool stop_requested() const {
                return _graph->_stop_flag.load(std::memory_order_relaxed);
            }

            void request_stop() {
                _graph->_stop_flag.store(true, std::memory_order_release);
            }

            void prepare_run() {
                _graph->reset_run_state();
                _graph->initialize_block_stats();
            }

            void launch_all_blocks(const FlowGraphConfig& config) {
                _graph->launch_tasks_impl(std::make_index_sequence<_N>{}, config);
            }

            void reset_stop_flag() {
                _graph->_stop_flag.store(false, std::memory_order_release);
            }

            void mark_block_start_times() {
                auto start_time = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < _N; ++i) {
                    _graph->_block_start_times[i] = start_time;
                }
            }

            void launch_may_block_tasks(const FlowGraphConfig& config) {
                _graph->template launch_may_block_tasks_impl<false>(
                    std::make_index_sequence<_N>{}, config, NoProgressNotification{});
            }

            template<typename ProgressNotifier>
            void launch_may_block_tasks_sampled(const FlowGraphConfig& config, ProgressNotifier notify_progress) {
                _graph->template launch_may_block_tasks_impl<true>(
                    std::make_index_sequence<_N>{}, config, notify_progress);
            }

            void warn_unresolved_edges() {
                for (size_t i = 0; i < _graph->_unresolved_edge_count; ++i) {
                    TaskPolicy::warn_unresolved_edge(_graph->block_name(_graph->_unresolved_edges[i].producer),
                                                     _graph->_unresolved_edges[i].address);
                }
            }

            void note_affinity_failure() {
                _graph->_affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }

            void rebuild_partition(size_t worker_count, bool use_costs, Partition& out) {
                std::array<uint8_t, _N> ids{};
                const size_t count = collect_regular_ids(ids);
                _graph->rebuild_partition_from(ids, count, worker_count, use_costs, out);
            }

            void collect_block_weights(const Partition& partition, std::array<double, _N>& weights) {
                sched::detail::collect_cost_weights<_N>(_graph->_cost_samples, partition.block_ids,
                                                        partition.block_count, weights);
                sched::detail::fill_unsampled_with_median<_N>(partition.block_ids,
                                                              partition.block_count, weights);
            }

            void mark_item_counters() { _graph->mark_item_counters(); }

            size_t collect_regular_blocks(std::array<uint8_t, _N>& ids) {
                return collect_regular_ids(ids);
            }

            void launch_task_per_regular_block(const FlowGraphConfig& config) {
                std::array<uint8_t, _N> ids{};
                const size_t count = collect_regular_ids(ids);
                for (size_t idx = 0; idx < count; ++idx) {
                    _graph->template launch_thread_per_block_task_dispatch_impl<false>(
                        std::make_index_sequence<_N>{}, ids[idx], config, NoProgressNotification{});
                }
            }

            template<typename Callable>
            void add_task(Callable&& callable) {
                _graph->_tasks[_graph->_active_task_count] =
                    TaskPolicy::create_task(std::forward<Callable>(callable));
                _graph->_active_task_count++;
            }

            bool execute_block(size_t index, const FlowGraphConfig& config) {
                return _graph->template execute_block_at_index<false>(index, config);
            }

            bool execute_block_sampled(size_t index, const FlowGraphConfig& config) {
                return _graph->template execute_block_at_index<true>(index, config);
            }

            void add_block_dead_time(size_t index, double seconds) {
                _graph->_stats[index].total_dead_time_s += seconds;
            }

            void set_block_runtime_from_start(size_t index) {
                auto end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> total_runtime = end_time - _graph->_block_start_times[index];
                _graph->_stats[index].total_runtime_s = total_runtime.count();
            }

        private:
            size_t collect_regular_ids(std::array<uint8_t, _N>& ids) {
                size_t count = 0;
                _graph->collect_regular_block_ids_impl(std::make_index_sequence<_N>{}, ids, count);
                return count;
            }

            FlowGraph* _graph;
        };

        using ThreadPerBlockScheduler = sched::ThreadPerBlockScheduler<SchedulerHost>;
        using FixedThreadPoolScheduler = sched::FixedThreadPoolScheduler<SchedulerHost>;
        using PinnedIslandsScheduler = sched::PinnedIslandsScheduler<SchedulerHost>;

        template <typename F>
        void for_each_block(F&& f) {
            std::apply([&](auto&... runners) { (f(*runners.block), ...); }, _runners);
        }

        void run(const FlowGraphConfig& config = FlowGraphConfig{}) {
            _config = config;
            _joined = false;
            _stop_flag.store(false, std::memory_order_release);
            
            
            switch (config.scheduler) {
                case SchedulerType::ThreadPerBlock: {
                    SchedulerHost host(*this);
                    ThreadPerBlockScheduler::start(host, _thread_state, config);
                    break;
                }
                    
                case SchedulerType::FixedThreadPool: {
                    SchedulerHost host(*this);
                    FixedThreadPoolScheduler::start(host, _fixed_state, config);
                    break;
                }

                case SchedulerType::PinnedIslands: {
                    SchedulerHost host(*this);
                    PinnedIslandsScheduler::start(host, _pinned_state, config);
                    break;
                }
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

            switch (_config.scheduler) {
                case SchedulerType::ThreadPerBlock: {
                    SchedulerHost host(*this);
                    ThreadPerBlockScheduler::notify_stop(host, _thread_state);
                    break;
                }

                case SchedulerType::FixedThreadPool: {
                    SchedulerHost host(*this);
                    FixedThreadPoolScheduler::notify_stop(host, _fixed_state);
                    break;
                }

                case SchedulerType::PinnedIslands: {
                    SchedulerHost host(*this);
                    PinnedIslandsScheduler::notify_stop(host, _pinned_state);
                    break;
                }
            }

            for (size_t i = 0; i < _active_task_count; ++i) {
                TaskPolicy::join_task(_tasks[i]);
            }
            _joined = true;
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_acquire);
        }

        // Only between stop() and run(): a self-terminated graph still has
        // workers unwinding until stop() joins them.
        void reset() {
            if (!_joined) TaskPolicy::fatal("FlowGraph::reset()", "called while workers are running; stop() first");
            std::apply([](auto&... runners) {
                (std::apply([](auto*... outs) { (outs->reset(), ...); }, runners.outputs), ...);
            }, _runners);
        }

        const FlowGraphConfig& config() const { return _config; }
        const std::array<BlockExecutionStats, _N>& stats() const { return _stats; }

        std::array<BlockCost, _N> block_costs() const {
            std::array<BlockCost, _N> out{};
            for (size_t i = 0; i < _N; ++i) {
                out[i].ewma_ns_per_call = sched::detail::bits_to_double(_cost_samples[i].ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                out[i].ewma_items_per_call = sched::detail::bits_to_double(_cost_samples[i].ewma_items_per_call_bits.load(std::memory_order_relaxed));
            }
            return out;
        }

        const Partition& partition() const { return _pinned_state.partition; }

        IslandCheck check_islands(const FlowGraphConfig& config) const {
            std::array<uint8_t, _N> ids{};
            size_t count = 0;
            for (size_t i = 0; i < _N; ++i) {
                if (_is_regular_block[i]) ids[count++] = static_cast<uint8_t>(i);
            }
            Partition scratch;
            return build_manual_partition(config, ids, count,
                                          (std::max)(config.num_workers, config.pinned_islands.island_count),
                                          scratch);
        }
        size_t repartition_count() const { return _pinned_state.barrier.count(); }
        size_t affinity_failure_count() const { return _affinity_failures.load(std::memory_order_relaxed); }

        uint64_t total_park_events() const {
            return _pinned_state.park_states.total_park_events();
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
        void collect_regular_flags_impl(std::index_sequence<Is...>) {
            ((_is_regular_block[Is] = !std::get<Is>(_runners).may_block), ...);
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
            collect_regular_flags_impl(std::make_index_sequence<_N>{});
            std::array<BlockSpan, _N> spans{};
            collect_spans_impl(std::make_index_sequence<_N>{}, spans);
            collect_edges_impl(std::make_index_sequence<_N>{}, spans);
        }

        

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

        template<std::size_t I, bool CostSampling>
        Result<Empty, Error> sample_and_invoke_procedure() {
            auto& runner = std::get<I>(_runners);

            if constexpr (!CostSampling) {
                return std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
            } else {
            auto& sample = _cost_samples[I];

            if (--sample.calls_until_sample != 0) {
                return std::apply([&](auto*... outs) {
                    return runner.block->procedure(outs...);
                }, runner.outputs);
            }
            sample.calls_until_sample = sched::detail::COST_SAMPLE_PERIOD_CALLS;

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

                const double prev_ns = sched::detail::bits_to_double(sample.ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                sample.ewma_ns_per_call_bits.store(sched::detail::double_to_bits(prev_ns + (observed_ns - prev_ns) / 8.0),
                                                   std::memory_order_relaxed);

                const double prev_items = sched::detail::bits_to_double(sample.ewma_items_per_call_bits.load(std::memory_order_relaxed));
                sample.ewma_items_per_call_bits.store(sched::detail::double_to_bits(prev_items + (observed_items - prev_items) / 8.0),
                                                      std::memory_order_relaxed);
            }

            return result;
            }
        }

    public:
        
        struct NoProgressNotification {
            void operator()() const {}
        };

        template<std::size_t I, bool CostSampling, typename ProgressNotifier>
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

                    Result<Empty, Error> result = sample_and_invoke_procedure<I, CostSampling>();

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
                run_block_at_index_thread_per_block<Is, false>(config, NoProgressNotification{});
            })), ...);
            _active_task_count = _N;  // ThreadPerBlock creates one task per block
        }
        
        template<std::size_t... Is>
        void init_stats_impl(std::index_sequence<Is...>) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            ((_stats[Is].name = std::get<Is>(_runners).block->name()), ...);
        }
        
        template<bool CostSampling, std::size_t... Is>
        bool execute_block_dispatch_impl(std::index_sequence<Is...>, size_t index, const FlowGraphConfig& config) {
            static_assert(((Is < _N) && ...), "All block indices must be within bounds");
            bool result = false;
            (void)((index == Is ? (result = execute_block_at_index_helper<Is, CostSampling>(config), true) : false) || ...);
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

        template<std::size_t I, bool CostSampling, typename ProgressNotifier>
        void launch_may_block_task_for_index(const FlowGraphConfig& config, ProgressNotifier notify_progress) {
            if (std::get<I>(_runners).may_block) {
                _tasks[_active_task_count++] = TaskPolicy::create_task([this, config, notify_progress]() {
                    run_block_at_index_thread_per_block<I, CostSampling>(config, notify_progress);
                });
            }
        }

        template<bool CostSampling, typename ProgressNotifier, std::size_t... Is>
        void launch_may_block_tasks_impl(std::index_sequence<Is...>, const FlowGraphConfig& config,
                                         ProgressNotifier notify_progress) {
            (launch_may_block_task_for_index<Is, CostSampling>(config, notify_progress), ...);
        }

        template<bool CostSampling, typename ProgressNotifier, std::size_t... Is>
        void launch_thread_per_block_task_dispatch_impl(std::index_sequence<Is...>, size_t index,
                                                        const FlowGraphConfig& config,
                                                        ProgressNotifier notify_progress) {
            (void)((index == Is ? (_tasks[_active_task_count++] = TaskPolicy::create_task([this, config, notify_progress]() {
                run_block_at_index_thread_per_block<Is, CostSampling>(config, notify_progress);
            }), true) : false) || ...);
        }


        typename ThreadPerBlockScheduler::State _thread_state;
        typename FixedThreadPoolScheduler::State _fixed_state;
        typename PinnedIslandsScheduler::State _pinned_state;
        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag{false};
        bool _joined = true;
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        std::array<sched::detail::SchedulerCostSample, _N> _cost_samples;
        std::array<const BlockBase*, _N> _block_bases{};
        std::array<bool, _N> _is_regular_block{};
        std::array<Edge, MaxEdges> _edges{};
        size_t _edge_count = 0;
        std::array<UnresolvedEdge, MaxEdges> _unresolved_edges{};
        size_t _unresolved_edge_count = 0;
        std::array<InputCounter, MaxEdges> _input_counters{};
        size_t _input_counter_count = 0;
        std::array<std::size_t, _N> _items_mark{};
        std::chrono::steady_clock::time_point _items_mark_time{};
        OnErrTerminateCallback _on_err_terminate_cb = nullptr;
        void* _on_err_terminate_context = nullptr;
        std::array<std::chrono::high_resolution_clock::time_point, _N> _block_start_times;
        size_t _active_task_count{0};  // Track actual created tasks to fix stop() hang

        std::atomic<size_t> _affinity_failures{0};

        void initialize_block_stats() {
            if (_config.collect_detailed_stats) {
                init_stats_impl(std::make_index_sequence<_N>{});
            }
        }
        

        void rebuild_partition_from(const std::array<uint8_t, _N>& regular_ids, size_t regular_count,
                                    size_t worker_count, bool use_costs, Partition& out) {
            const auto& pinned = _config.pinned_islands;
            if (pinned.islands != nullptr || pinned.island_names != nullptr) {
                const IslandCheck check =
                    build_manual_partition(_config, regular_ids, regular_count, worker_count, out);
                if (!check) {
                    TaskPolicy::fatal(check.message(),
                                      check.block != nullptr ? check.block->name() : "");
                }
            } else {
                std::array<double, _N> weights{};
                sched::detail::collect_cost_weights<_N>(_cost_samples, regular_ids, regular_count, weights);
                sched::detail::build_partition<_N, DEFAULT_MAX_WORKERS>(
                    _edges.data(), _edge_count, weights,
                    regular_ids, regular_count, worker_count,
                    use_costs, _unresolved_edge_count == 0, out);
            }
            if (pinned.report_partition) {
                report_partition(out);
            }
        }

        size_t index_of_block(const BlockBase* block) const {
            for (size_t i = 0; i < _N; ++i) {
                if (_block_bases[i] == block) return i;
            }
            return _N;
        }

        static bool token_equals(const char* begin, const char* end, const char* name) {
            while (begin != end && (*begin == ' ' || *begin == '\t' || *begin == '\n')) ++begin;
            while (end != begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n')) --end;
            const size_t length = static_cast<size_t>(end - begin);
            return std::strlen(name) == length && std::strncmp(begin, name, length) == 0;
        }

        size_t index_of_name(const char* begin, const char* end) const {
            for (size_t i = 0; i < _N; ++i) {
                if (token_equals(begin, end, _block_bases[i]->name())) return i;
            }
            return _N;
        }

        static bool is_regular(size_t index, const std::array<uint8_t, _N>& regular_ids,
                               size_t regular_count) {
            for (size_t i = 0; i < regular_count; ++i) {
                if (regular_ids[i] == index) return true;
            }
            return false;
        }

        IslandCheck add_island_block(size_t index, size_t island,
                                     const std::array<uint8_t, _N>& regular_ids, size_t regular_count,
                                     std::array<bool, _N>& used, Partition& out, size_t& cursor) const {
            if (!is_regular(index, regular_ids, regular_count)) {
                return IslandCheck{IslandCheck::Code::NotSchedulable, island, _block_bases[index]};
            }
            if (used[index]) {
                return IslandCheck{IslandCheck::Code::Duplicate, island, _block_bases[index]};
            }
            used[index] = true;
            out.block_ids[cursor++] = static_cast<uint8_t>(index);
            return IslandCheck{};
        }

        IslandCheck append_island_blocks(const IslandList& list, size_t island,
                                         const std::array<uint8_t, _N>& regular_ids, size_t regular_count,
                                         std::array<bool, _N>& used, Partition& out, size_t& cursor) const {
            for (size_t k = 0; k < list.count; ++k) {
                const BlockBase* block = list.blocks[k];
                const size_t index = index_of_block(block);
                if (index == _N) {
                    return IslandCheck{IslandCheck::Code::NotInGraph, island, block};
                }
                const IslandCheck check =
                    add_island_block(index, island, regular_ids, regular_count, used, out, cursor);
                if (!check) return check;
            }
            return IslandCheck{};
        }

        IslandCheck append_island_names(const char* spec, size_t island,
                                        const std::array<uint8_t, _N>& regular_ids, size_t regular_count,
                                        std::array<bool, _N>& used, Partition& out, size_t& cursor) const {
            if (spec == nullptr) return IslandCheck{IslandCheck::Code::EmptyIsland, island};

            const char* current = spec;
            while (*current != '\0') {
                const char* comma = std::strchr(current, ',');
                const char* end = comma != nullptr ? comma : current + std::strlen(current);
                const size_t index = index_of_name(current, end);
                if (index == _N) {
                    return IslandCheck{IslandCheck::Code::NotInGraph, island, nullptr};
                }
                const IslandCheck check =
                    add_island_block(index, island, regular_ids, regular_count, used, out, cursor);
                if (!check) return check;
                current = comma != nullptr ? comma + 1 : end;
            }
            return IslandCheck{};
        }

        IslandCheck check_within_island_order(const Partition& out) const {
            std::array<uint16_t, _N> position{};
            std::array<uint8_t, _N> island_of{};
            std::array<bool, _N> listed{};

            for (size_t island = 0; island < out.island_count; ++island) {
                for (uint16_t k = out.island_begin[island]; k < out.island_begin[island + 1]; ++k) {
                    const uint8_t id = out.block_ids[k];
                    position[id] = k;
                    island_of[id] = static_cast<uint8_t>(island);
                    listed[id] = true;
                }
            }

            for (size_t e = 0; e < _edge_count; ++e) {
                const uint8_t producer = _edges[e].producer;
                const uint8_t consumer = _edges[e].consumer;
                if (producer == consumer) continue;
                if (!listed[producer] || !listed[consumer]) continue;
                if (island_of[producer] != island_of[consumer]) continue;
                if (position[producer] >= position[consumer]) {
                    return IslandCheck{IslandCheck::Code::NotTopological, island_of[consumer],
                                       _block_bases[consumer]};
                }
            }
            return IslandCheck{};
        }

        IslandCheck build_manual_partition(const FlowGraphConfig& config,
                                           const std::array<uint8_t, _N>& regular_ids, size_t regular_count,
                                           size_t worker_count, Partition& out) const {
            const auto& pinned = config.pinned_islands;
            if (pinned.islands != nullptr && pinned.island_names != nullptr) {
                return IslandCheck{IslandCheck::Code::BothFormsGiven};
            }
            if (pinned.island_count == 0) return IslandCheck{IslandCheck::Code::NoIslands};
            if (pinned.island_count > worker_count) {
                return IslandCheck{IslandCheck::Code::TooManyIslands};
            }

            std::array<bool, _N> used{};
            size_t cursor = 0;

            for (size_t island = 0; island < pinned.island_count; ++island) {
                out.island_begin[island] = static_cast<uint16_t>(cursor);
                const IslandCheck check =
                    pinned.islands != nullptr
                        ? append_island_blocks(pinned.islands[island], island, regular_ids,
                                               regular_count, used, out, cursor)
                        : append_island_names(pinned.island_names[island], island, regular_ids,
                                              regular_count, used, out, cursor);
                if (!check) return check;
                if (cursor == out.island_begin[island]) {
                    return IslandCheck{IslandCheck::Code::EmptyIsland, island};
                }
            }

            out.island_begin[pinned.island_count] = static_cast<uint16_t>(cursor);
            out.block_count = static_cast<uint16_t>(cursor);
            out.island_count = static_cast<uint16_t>(pinned.island_count);

            for (size_t i = 0; i < regular_count; ++i) {
                if (!used[regular_ids[i]]) {
                    return IslandCheck{IslandCheck::Code::Missing, 0, _block_bases[regular_ids[i]]};
                }
            }
            return check_within_island_order(out);
        }

        void report_partition(const Partition& partition) {
            std::array<double, _N> weights{};
            collect_utilisation_weights(weights);
            sched::detail::fill_unsampled_with_median<_N>(partition.block_ids,
                                                          partition.block_count, weights);
            for (size_t island = 0; island < partition.island_count; ++island) {
                for (uint16_t k = partition.island_begin[island]; k < partition.island_begin[island + 1]; ++k) {
                    const uint8_t id = partition.block_ids[k];
                    TaskPolicy::report_partition_block(island, k, block_name(id), weights[id]);
                }
            }
        }

        template<size_t I>
        std::size_t cumulative_items_for_index() const {
            std::size_t reads = 0;
            bool has_inputs = false;
            for (size_t i = 0; i < _input_counter_count; ++i) {
                const auto& ic = _input_counters[i];
                if (ic.consumer != static_cast<uint8_t>(I)) continue;
                has_inputs = true;
                const std::size_t count = ic.read_count(ic.channel);
                if (count > reads) reads = count;
            }
            if (has_inputs) return reads;
            return sum_output_cumulative_write_count(std::get<I>(_runners).outputs);
        }

        template<std::size_t... Is>
        void fill_cumulative_items(std::array<std::size_t, _N>& items, std::index_sequence<Is...>) const {
            ((items[Is] = cumulative_items_for_index<Is>()), ...);
        }

        void mark_item_counters() {
            fill_cumulative_items(_items_mark, std::make_index_sequence<_N>{});
            _items_mark_time = std::chrono::steady_clock::now();
        }

        void collect_utilisation_weights(std::array<double, _N>& weights) {
            std::array<std::size_t, _N> items{};
            fill_cumulative_items(items, std::make_index_sequence<_N>{});

            const double elapsed_s =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - _items_mark_time).count();

            for (size_t i = 0; i < _N; ++i) {
                const double ns = sched::detail::ns_per_item(_cost_samples[i]);
                const std::size_t delta = items[i] >= _items_mark[i] ? items[i] - _items_mark[i] : 0;
                const double items_per_second =
                    elapsed_s > 0.0 ? static_cast<double>(delta) / elapsed_s : 0.0;
                weights[i] = ns * items_per_second;
            }
        }

        void reset_run_state() {
            for (auto& stats : _stats) stats = BlockExecutionStats{};
            for (auto& sample : _cost_samples) {
                sample.calls_until_sample = sched::detail::COST_SAMPLE_PERIOD_CALLS;
                sample.ewma_ns_per_call_bits.store(0, std::memory_order_relaxed);
                sample.ewma_items_per_call_bits.store(0, std::memory_order_relaxed);
            }
            _pinned_state.reset();
            _affinity_failures.store(0, std::memory_order_relaxed);
            _active_task_count = 0;
        }

        template<size_t I, bool CostSampling>
        bool execute_block_at_index_helper(const FlowGraphConfig& config) {
            static_assert(I < _N, "Block index out of bounds");

            auto& stats = _stats[I];

            bool did_work = false;

            for (size_t c = 0; c < config.max_calls_per_tick; ++c) {
                auto result = sample_and_invoke_procedure<I, CostSampling>();

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

        template<bool CostSampling>
        bool execute_block_at_index(size_t index, const FlowGraphConfig& config) {
            return execute_block_dispatch_impl<CostSampling>(std::make_index_sequence<_N>{}, index, config);
        }
        
    };

    constexpr float PI = 3.14159265358979323846f;

} // namespace cler