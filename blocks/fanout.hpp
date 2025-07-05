#pragma once

#include "cler.hpp"

// A generic Demux block: routes input samples to multiple outputs in round-robin fashion.
template <typename T>
struct FanoutBlock : public cler::BlockBase {
    cler::Channel<T> in;  // Single input channel

    FanoutBlock(const char* name, size_t num_outputs)
        : cler::BlockBase(name), _num_outputs(num_outputs) {
        _tmp = new T[DEFAULT_BUFFER_SIZE];
    }

    ~FanoutBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>** outs) {
        if (_num_outputs < 1) {
            return cler::Error::InvalidArgument;
        }

        // How many input samples can we read?
        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // How much space is available in the outputs?
        size_t min_output_space = outs[0]->space();
        for (size_t i = 1; i < _num_outputs; ++i) {
            if (outs[i]->space() < min_output_space) {
                min_output_space = outs[i]->space();
            }
        }
        if (min_output_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transferable = cler::floor2(std::min(available_samples, min_output_space, cler::DEFAULT_BUFFER_SIZE));

        in.readN(_tmp, transferable);
        for (size_t i = 0; i < _num_outputs; ++i) {
            outs[i]->writeN(_tmp, transferable);
        }

        return cler::Empty{};
    }

    private:
        size_t _num_outputs;
        T* _tmp;
};
