#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include <array>
#include <algorithm>
#include <complex>
#include <string>
#include <atomic>
#include <chrono>

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
            case Error::NotEnoughSpace:
                return "Not enough space in output buffers";
            case Error::NotEnoughSamples:
                return "Not enough samples in input buffers";
            case Error::ProcedureError:
                return "Procedure error";
            case Error::BadData:
                return "Bad data received";
            case Error::TERM_InvalidChannelIndex:
                return "TERM: Invalid channel index";
            case Error::TERM_ProcedureError:
                return "TERM: Procedure error";
            case Error::TERM_IOError:
                return "TERM: IO error";
            case Error::TERM_EOFReached:
                return "TERM: EOF reached";
            default:
                return "Unknown error";
        }
    }

    // Threading policies are now in separate headers:
    // - cler_stdthread_policy.hpp for desktop std::thread
    // - cler_freertos_policy.hpp for FreeRTOS
    // - cler_threadx_policy.hpp for ThreadX


    // Channel now uses the existing SPSC queue directly
    template <typename T, size_t N = 0, typename Allocator = std::allocator<T>>
    using Channel = dro::SPSCQueue<T, N, Allocator>;

    struct BlockBase {
        explicit BlockBase(std::string&& name) : _name(name) {}
        const std::string& name() const { return _name; }

        BlockBase(const BlockBase&) = delete;
        BlockBase& operator=(const BlockBase&) = delete;
        BlockBase(BlockBase&&) = delete;
        BlockBase& operator=(BlockBase&&) = delete;
    private:
       std::string _name;
    };

    template<typename Block, typename... Channels>
    struct BlockRunner {
        Block* block;
        std::tuple<Channels*...> outputs;

        BlockRunner(Block* blk, Channels*... outs)
            : block(blk), outputs(outs...) {}
    };

    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, Channels...>;

    struct BlockExecutionStats {
        std::string name;
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

    // Unified FlowGraph with threading policy
    template<typename ThreadingPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        using thread_type = typename ThreadingPolicy::thread_type;
        typedef void (*OnCrashCallback)(void* context);

        FlowGraph(BlockRunners... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(std::move(runners))...)) {
        }

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

            auto launch_threads = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_threads[Is] = ThreadingPolicy::create_thread([this, Is]() {
                    this->run_block(Is);
                })), ...);
            };

            launch_threads(std::make_index_sequence<_N>{});
        }

        void stop() {
            _stop_flag = true;
            for (auto& t : _threads) {
                ThreadingPolicy::join_thread(t);
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_seq_cst);
        }

        const FlowGraphConfig& config() const {
            return _config;
        }

        const std::array<BlockExecutionStats, _N>& stats() const {
            return _stats;
        }

    private:
        template<size_t I>
        void run_block(size_t index) {
            auto& runner = std::get<I>(_runners);
            auto& stats  = _stats[index];

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

                            ThreadingPolicy::sleep_us(static_cast<size_t>(adaptive_sleep_us));
                        } else {
                            ThreadingPolicy::yield();
                        }

                    } else {
                        ThreadingPolicy::yield();
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
        }

        static constexpr std::size_t _N = sizeof...(BlockRunners);
        std::tuple<BlockRunners...> _runners;
        std::array<thread_type, _N> _threads;
        std::atomic<bool> _stop_flag = false;
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        OnCrashCallback _on_crash_cb = nullptr;
        void* _on_crash_context = nullptr;
    };

    static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;

} // namespace cler