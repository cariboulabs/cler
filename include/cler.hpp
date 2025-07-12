#pragma once

#include "SPSC-Queue/include/dro/spsc-queue.hpp"
#include "cler_result.hpp"
#include <thread>
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <string> //block names are self owning strings

namespace cler {

    enum class Error {
        InvalidChannelIndex,
        NotEnoughSamples,
        NotEnoughSpace,
        ProcedureError,
        IOError,
        EOFReached,
    };

    inline const char* to_str(Error error) {
        switch (error) {
            case Error::InvalidChannelIndex:
                return "Invalid channel index";
            case Error::NotEnoughSpace:
                return "Not enough space in output buffers";
            case Error::NotEnoughSamples:
                return "Not enough samples in input buffers";
            case Error::ProcedureError:
                return "Procedure error";
            case Error::IOError:
                return "I/O error";
            case Error::EOFReached:
                return "End of file reached";
            default:
                return "Unknown error";
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

    //allows for heap/stack allocation to share the same interface
    template <typename T, size_t N = 0>
    struct Channel : public ChannelBase<T> {
        dro::SPSCQueue<T, N> _queue;

        Channel() = default;
        Channel(size_t size) requires (N == 0) : _queue(size) {
            if (size == 0) {
                throw std::invalid_argument("Channel size must be greater than zero.");
            }
        }

        size_t size() const override { return _queue.size(); }
        size_t space() const override { return _queue.space(); }

        void push(const T& v) override { _queue.push(v); }
        void pop(T& v) override { _queue.pop(v); }

        bool try_push(const T& v) override {
            return _queue.try_push(v);
        }
        bool try_pop(T& v) override {
            return _queue.try_pop(v);
        }

        size_t writeN(const T* data, size_t n) override {
            return _queue.writeN(data, n);
        }
        size_t readN(T* data, size_t n) override {
            return _queue.readN(data, n);
        }
        size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) override {
            return _queue.peek_write(ptr1, size1, ptr2, size2);
        }

        void commit_write(size_t count) override {
            _queue.commit_write(count);
        }

        size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) override {
            return _queue.peek_read(ptr1, size1, ptr2, size2);
        }

        void commit_read(size_t count) override {
            _queue.commit_read(count);
        }
    };

    struct BlockBase {
        explicit BlockBase(std::string name) : _name(std::move(name)) {}
        const std::string& name() const { return _name; }

        // Non-copyable
        BlockBase(const BlockBase&) = delete;
        BlockBase& operator=(const BlockBase&) = delete;

        // Non-movable
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
    // Deduction guide for BlockRunner (C++17 and later)
    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, Channels...>;

    struct BlockExecutionStats {
        std::string name;
        size_t successful_procedures = 0;
        size_t failed_procedures = 0;
        double avg_dead_time_us = 0.0;
        double total_dead_time_s = 0.0;
        double final_adaptive_sleep_us = 0.0;
        size_t final_consecutive_fails = 0;
        double total_runtime_s = 0.0;
    };

    struct FlowGraphConfig {
        bool adaptive_sleep = true;
    };

    template<typename... BlockRunners>
    class FlowGraph {
    public:
        FlowGraph(BlockRunners&... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(runners)...)) {
            _stats.resize(sizeof...(BlockRunners));
        }

        ~FlowGraph() { stop(); }

        FlowGraph(const FlowGraph&) = delete;
        FlowGraph(FlowGraph&&) = delete;
        FlowGraph& operator=(const FlowGraph&) = delete;
        FlowGraph& operator=(FlowGraph&&) = delete;

        void run(FlowGraphConfig config = FlowGraphConfig{}) {
            _config = config;
            _stop_flag = false;

            auto launch_threads = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_threads[Is] = std::thread([this]() {
                    auto& runner = std::get<Is>(_runners);
                    auto& stats  = _stats[Is];

                    stats.name = runner.block->name();

                    auto t_start = std::chrono::steady_clock::now();
                    auto t_last  = t_start;

                    size_t successful = 0;
                    size_t failed = 0;
                    double avg_dead = 0.0;
                    double total_dead = 0.0;
                    double sleep_us = 0.0;
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
                            if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                                total_dead += dt.count();
                                avg_dead += (dt.count() - avg_dead) / failed;
                                consecutive_fails++;

                                if (_config.adaptive_sleep) {
                                    if (consecutive_fails > 50) {
                                        sleep_us = std::min(sleep_us * 1.5 + 1.0, 5000.0);
                                    }
                                    double desired = avg_dead * 1e6 * 0.5;
                                    sleep_us = std::max(sleep_us, desired);
                                    std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(sleep_us)));
                                } else {
                                    std::this_thread::yield();
                                }
                            } else {
                                std::this_thread::yield();
                            }
                        } else {
                            successful++;
                            consecutive_fails = 0;
                            sleep_us *= 0.8;
                        }
                    }

                    auto t_end = std::chrono::steady_clock::now();
                    std::chrono::duration<double> total_time = t_end - t_start;

                    stats.successful_procedures = successful;
                    stats.failed_procedures = failed;
                    stats.avg_dead_time_us = avg_dead * 1e6;
                    stats.total_dead_time_s = total_dead;
                    stats.final_adaptive_sleep_us = _config.adaptive_sleep ? sleep_us : 0.0;
                    stats.final_consecutive_fails = consecutive_fails;
                    stats.total_runtime_s = total_time.count();
                })), ...);
            };

            launch_threads(std::make_index_sequence<_N>{});
        }

        void stop() {
            _stop_flag = true;
            for (auto& t : _threads) {
                if (t.joinable()) t.join();
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_seq_cst);
        }

        const FlowGraphConfig& config() const {
            return _config;
        }

        const std::vector<BlockExecutionStats>& stats() const {
            return _stats;
        }

    private:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        std::tuple<BlockRunners...> _runners;
        std::array<std::thread, _N> _threads;
        std::atomic<bool> _stop_flag = false;
        FlowGraphConfig _config;
        std::vector<BlockExecutionStats> _stats;
    };

    static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;

} // namespace cler