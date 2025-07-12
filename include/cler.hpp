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

    template<typename... BlockRunners>
    class FlowGraph {
    public:
        FlowGraph(BlockRunners&... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(runners)...)) {}

        ~FlowGraph() {
            stop(); // Ensure threads are stopped before destruction
        }

        //delete copy, move and assignment. Once created that thing does not go anywhere
        FlowGraph(const FlowGraph&) = delete;
        FlowGraph(FlowGraph&&) = delete;
        FlowGraph& operator=(const FlowGraph&) = delete;
        FlowGraph& operator=(FlowGraph&&) = delete;

        void run(const bool print_execution_report = true) {
            _print_execution_report = print_execution_report;
            _stop_flag = false;

            // This lambda takes an index sequence (0, 1, ..., N-1) and expands it at compile time.
            auto launch_threads = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_threads[Is] = std::thread([this]() {
                    auto& runner = std::get<Is>(_runners);
                    size_t succesful_procedures = 0;
                    size_t failed_procedures = 0;

                    while (!_stop_flag) {
                        Result<Empty, Error> result = std::apply([&](auto*... outs) {
                            return runner.block->procedure(outs...);
                        }, runner.outputs);
                        if (result.is_err()) {
                            failed_procedures++;
                            auto err = result.unwrap_err();
                            if (err == Error::InvalidChannelIndex) {
                                stop();
                                throw std::runtime_error(to_str(err)); //only crashes the current thread, so we stop
                            } else {
                                std::this_thread::yield();
                            }
                        } else {
                            succesful_procedures++;
                        }
                    }
                    
                    if (_print_execution_report) {
                        size_t total_procedures = succesful_procedures + failed_procedures;
                        float success_rate = total_procedures > 0 ? 
                            (static_cast<float>(succesful_procedures) / total_procedures) * 100.0f : 0.0f;
                        printf("Block %s finished with success rate: %.2f%%\n", 
                            runner.block->name().c_str(), success_rate);
                    }

                })), ...);
            };

            // Generate the index sequence and call the lambda to launch threads
            launch_threads(std::make_index_sequence<_N>{});
        }

        void stop() {
            _stop_flag = true;
            for (auto& t : _threads) {
                if (t.joinable()) t.join();
            }

            if (_print_execution_report) {
                printf("Execution Report Warning!!!:\n");
                printf("If the flowgraph is working correctly, then blocks with a *higher* "
                       "procedure success rates can be the bottlenecks that block traffic.\n");
                printf("This is a classic case of `survivor bias`\n");
            }
        }

    private:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        std::tuple<BlockRunners...> _runners;
        std::array<std::thread, _N> _threads;
        std::atomic<bool> _stop_flag = false;
        bool _print_execution_report = false;
    };
    
    static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;
}