#pragma once

#include "SPSC-Queue/include/dro/spsc-queue.hpp"
#include "result.hpp"
#include <thread>
#include <array>

enum class ClerError {
    InvalidChannelIndex,
    NotEnoughSamples,
    NotEnoughSpace,    
};

inline const char* to_str(ClerError error) {
    switch (error) {
        case ClerError::InvalidChannelIndex:
            return "Invalid channel index";
        case ClerError::NotEnoughSpace:
            return "Not enough space in output buffers";
        case ClerError::NotEnoughSamples:
            return "Not enough samples in input buffers";
        default:
            return "Unknown error";
    }
}

namespace cler {
    template <typename T>
    using Channel = dro::SPSCQueue<T>;

    template <typename Derived>
    struct BlockBase {

        explicit BlockBase(const char* name) : _name(name) {}

        template <typename... OChannel>
        Result<Empty, ClerError> procedure(OChannel*... ochannels) {
            return static_cast<Derived*>(this)->procedure_impl(ochannels...);
        }

        const char* name() {
            return _name;
        }

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
            _stop_flag = false;

            // This lambda takes an index sequence (0, 1, ..., N-1) and expands it at compile time.
            auto launch_threads = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((_threads[Is] = std::thread([this]() {
                    auto& runner = std::get<Is>(_runners);

                    while (!_stop_flag) {
                        Result<Empty, ClerError> result = std::apply([&](auto*... outs) {
                            return runner.block->procedure(outs...);
                        }, runner.outputs);
                        if (result.is_err()) {
                            auto err = result.unwrap_err();
                            if (err == ClerError::InvalidChannelIndex) {
                                stop();
                                throw std::runtime_error(to_str(err)); //onlt crashes the current thread, so we stop
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
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        std::tuple<BlockRunners...> _runners;
        std::array<std::thread, _N> _threads;
        std::atomic<bool> _stop_flag = false;
    };
}