#pragma once

#include "SPSC-Queue/include/dro/spsc-queue.hpp"
#include "result.hpp"
#include <thread>
#include <array>
#include <set>

namespace cler {

    size_t floor2(size_t x) {
        if (x == 0) return 0;
        return size_t(1) << (std::bit_width(x) - 1);
    }

    enum class Error {
        InvalidChannelIndex,
        NotEnoughSamples,
        NotEnoughSpace,    
    };

    inline const char* to_str(Error error) {
        switch (error) {
            case Error::InvalidChannelIndex:
                return "Invalid channel index";
            case Error::NotEnoughSpace:
                return "Not enough space in output buffers";
            case Error::NotEnoughSamples:
                return "Not enough samples in input buffers";
            default:
                return "Unknown error";
        }
    }
    
    template <typename T, size_t N = 0>
    using Channel = dro::SPSCQueue<T, N>;

    struct BlockBase {
        explicit BlockBase(const char* name) : _name(name) {}
        const char* name() const { return _name; }
    private:
        const char* _name;
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

        void run() {
            if (!valid()) {
                throw std::runtime_error("Invalid FlowGraph: "
                        "duplicate channels detected. Each channel can be connected once.");
            }
            _stop_flag = false;

            // This lambda takes an index sequence (0, 1, ..., N-1) and expands it at compile time.
            auto launch_threads = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_threads[Is] = std::thread([this]() {
                    auto& runner = std::get<Is>(_runners);

                    while (!_stop_flag) {
                        Result<Empty, Error> result = std::apply([&](auto*... outs) {
                            return runner.block->procedure(outs...);
                        }, runner.outputs);
                        if (result.is_err()) {
                            auto err = result.unwrap_err();
                            if (err == Error::InvalidChannelIndex) {
                                stop();
                                throw std::runtime_error(to_str(err)); //only crashes the current thread, so we stop
                            } else {
                                std::this_thread::yield();
                            }
                        }
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
        }

    private:

        bool valid() const {
            std::set<const void*> output_ptrs;
            bool ok = true;

            //check that each output pointer is unique
            std::apply([&](const auto&... runners) {
                ((std::apply([&](auto*... outs) {
                    ((ok &= output_ptrs.insert(static_cast<const void*>(outs)).second), ...);
                }, runners.outputs)), ...);
            }, _runners);

            return ok;
        }

        static constexpr std::size_t _N = sizeof...(BlockRunners);
        std::tuple<BlockRunners...> _runners;
        std::array<std::thread, _N> _threads;
        std::atomic<bool> _stop_flag = false;
    };
}