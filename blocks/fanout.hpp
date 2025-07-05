#pragma once

#include "cler.hpp"

// A generic Demux block: routes input samples to multiple outputs in round-robin fashion.
template <typename T>
struct FanoutBlock : public cler::BlockBase {
    cler::Channel<T> in;  // Single input channel

    FanoutBlock(const char* name, size_t num_outputs)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE), _num_outputs(num_outputs) {
        _tmp = new T[cler::DEFAULT_BUFFER_SIZE];
    }

    ~FanoutBlock() {
        delete[] _tmp;
    }

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_outputs && "Number of output channels must match the number of polyphase channels");

        // How many input samples can we read?
        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // How much space is available in the outputs?
        size_t min_output_space = size_t(-1);
        auto check_space = [&](auto* out) {
            size_t space = out->space();
            if (space < min_output_space) {
                min_output_space = space;
            }
        };
        (check_space(outs), ...);  // Fold expression to check all output channels
        if (min_output_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transferable = cler::floor2(std::min({available_samples, min_output_space, cler::DEFAULT_BUFFER_SIZE}));

        in.readN(_tmp, transferable);
        auto write_to_output = [&](auto* out) {
            out->writeN(_tmp, transferable);
        };
        (write_to_output(outs), ...);  // Fold expression to write to all output

        return cler::Empty{};
    }

    private:
        size_t _num_outputs;
        T* _tmp;
};
