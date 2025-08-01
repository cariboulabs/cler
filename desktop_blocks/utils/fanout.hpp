#pragma once

#include "cler.hpp"
#include <cassert>

// A generic Demux block: routes input samples to multiple outputs in round-robin fashion.
template <typename T>
struct FanoutBlock : public cler::BlockBase {
    cler::Channel<T> in;  // Single input channel

    FanoutBlock(const char* name, const size_t num_outputs, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _num_outputs(num_outputs) {
        
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements of type T");
        }

        assert(num_outputs > 0 && "Number of outputs must be greater than zero");
    }

    ~FanoutBlock() = default;

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_outputs && "Number of output channels must match the number of polyphase channels");

        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        
        // Find minimum space available across all outputs
        size_t min_write_size = read_size;
        auto check_write_space = [&](auto* out) {
            auto [write_ptr, write_size] = out->write_dbf();
            min_write_size = std::min(min_write_size, write_size);
        };
        (check_write_space(outs), ...);
        
        if (min_write_size > 0) {
            // Copy to all outputs
            auto copy_to_output = [&](auto* out) {
                auto [write_ptr, write_size] = out->write_dbf();
                std::memcpy(write_ptr, read_ptr, min_write_size * sizeof(T));
                out->commit_write(min_write_size);
            };
            (copy_to_output(outs), ...);
            
            in.commit_read(min_write_size);
        }

        return cler::Empty{};
    }

    private:
        size_t _num_outputs;
};
