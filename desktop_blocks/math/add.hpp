#include "cler.hpp"

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in;

    AddBlock(const char* name, const size_t num_inputs, const size_t buffer_size = 1024)
        : cler::BlockBase(name), _num_inputs(num_inputs) { // Default 1024 for 4KB minimum

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
        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        
        // Check if all inputs have data available
        size_t min_available = write_size;
        for (size_t i = 0; i < _num_inputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            min_available = std::min(min_available, read_size);
        }
        
        if (min_available > 0) {
            // Zero the output buffer
            std::fill_n(write_ptr, min_available, T{});
            
            // Use read_dbf for each input
            for (size_t i = 0; i < _num_inputs; ++i) {
                auto [read_ptr, read_size] = in[i].read_dbf();
                // Direct add from input buffer
                for (size_t j = 0; j < min_available; ++j) {
                    write_ptr[j] += read_ptr[j];
                }
                in[i].commit_read(min_available);
            }
            
            out->commit_write(min_available);
        }
        return cler::Empty{};
    }

    private:
        size_t _num_inputs;
        size_t _buffer_size;
        T*  _tmp_buffer = nullptr;
        T*  _sum_buffer = nullptr;
};