#include "cler.hpp"

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in;

    AddBlock(const char* name, const size_t num_inputs, const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), _num_inputs(num_inputs) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }

        if (num_inputs < 2) {
            throw std::invalid_argument("AddBlock requires at least two input channels");
        }

        _buffer_size = buffer_size;
        
        // Our ringbuffers are not copy/move so we cant use std::vector
        // As such, we use a raw array of cler::Channel<T>
        // Allocate raw storage only, no default construction
        in = static_cast<cler::Channel<T>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<T>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<T>(buffer_size);
        }

        _tmp_buffer = new T[buffer_size];
        _sum_buffer = new T[buffer_size];
     }
    ~AddBlock() {
        using TChannel = cler::Channel<T>; //cant template on Destructor...
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].~TChannel();
        }
        ::operator delete[](in);
        delete[] _tmp_buffer;
        delete[] _sum_buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Try zero-copy path first with write_dbf
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr && write_size > 0) {
            // Check if all inputs have data available
            size_t min_available = SIZE_MAX;
            for (size_t i = 0; i < _num_inputs; ++i) {
                size_t avail = in[i].size();
                if (avail == 0) return cler::Error::NotEnoughSamples;
                min_available = std::min(min_available, avail);
            }
            
            size_t to_process = std::min(write_size, min_available);
            
            // Zero the output buffer
            std::fill_n(write_ptr, to_process, T{});
            
            // Try to use read_dbf for each input
            for (size_t i = 0; i < _num_inputs; ++i) {
                auto [read_ptr, read_size] = in[i].read_dbf();
                if (read_ptr && read_size >= to_process) {
                    // Direct add from input buffer
                    for (size_t j = 0; j < to_process; ++j) {
                        write_ptr[j] += read_ptr[j];
                    }
                    in[i].commit_read(to_process);
                } else {
                    // Fallback to readN for this input
                    in[i].readN(_tmp_buffer, to_process);
                    for (size_t j = 0; j < to_process; ++j) {
                        write_ptr[j] += _tmp_buffer[j];
                    }
                }
            }
            
            out->commit_write(to_process);
            return cler::Empty{};
        }

        // Fall back to standard approach
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t min_available_samples = in[0].size();
        for (size_t i = 1; i < _num_inputs; ++i) {
            if (in[i].size() < min_available_samples) {
                min_available_samples = in[i].size();
            }
        }
        if (min_available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t transferable = std::min({available_space, min_available_samples, _buffer_size});

        std::fill_n(_sum_buffer, _buffer_size, T{});
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].readN(_tmp_buffer, transferable);
            for (size_t j = 0; j < transferable; ++j) {
                _sum_buffer[j] += _tmp_buffer[j];
            }
        }
        out->writeN(_sum_buffer, transferable);
        return cler::Empty{};
    }

    private:
        size_t _num_inputs;
        size_t _buffer_size;
        T*  _tmp_buffer = nullptr;
        T*  _sum_buffer = nullptr;
};