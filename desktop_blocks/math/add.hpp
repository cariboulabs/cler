#include "cler.hpp"

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in = nullptr;

    AddBlock(const char* name, const size_t num_inputs, const size_t buffer_size = 0)
        : cler::BlockBase(name), _num_inputs(num_inputs) {

        // Calculate proper buffer size for type T
        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements of type T");
        }

        if (num_inputs < 2) {
            throw std::invalid_argument("AddBlock requires at least two input channels");
        }

        _buffer_size = actual_buffer_size;
        
        // Our ringbuffers are not copy/move so we cant use std::vector
        // As such, we use a raw array of cler::Channel<T>
        // Allocate raw storage only, no default construction
        try {
            in = static_cast<cler::Channel<T>*>(
                ::operator new[](num_inputs * sizeof(cler::Channel<T>))
            );
        } catch (const std::bad_alloc&) {
            throw std::runtime_error("Failed to allocate memory for input channels");
        }
        
        // Construct channels with exception safety
        size_t constructed_channels = 0;
        try {
            for (size_t i = 0; i < num_inputs; ++i) {
                new (&in[i]) cler::Channel<T>(actual_buffer_size);
                constructed_channels++;
            }
        } catch (...) {
            cleanup_channels(constructed_channels);
            throw std::runtime_error("Failed to construct input channels");
        }

        try {
            _tmp_buffer = new T[actual_buffer_size];
        } catch (const std::bad_alloc&) {
            cleanup_channels(_num_inputs);
            throw std::runtime_error("Failed to allocate temporary buffer");
        }
        
        try {
            _sum_buffer = new T[actual_buffer_size];
        } catch (const std::bad_alloc&) {
            delete[] _tmp_buffer;
            _tmp_buffer = nullptr;
            cleanup_channels(_num_inputs);
            throw std::runtime_error("Failed to allocate sum buffer");
        }
     }
    ~AddBlock() {
        cleanup_channels(_num_inputs);
        delete[] _tmp_buffer;
        delete[] _sum_buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        if (!write_ptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
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
            return cler::Empty{};
        } else {
            return cler::Error::NotEnoughSamples;
        }


    }

    private:
        void cleanup_channels(size_t count) {
            if (!in) return;
            using TChannel = cler::Channel<T>;
            for (size_t i = 0; i < count; ++i) {
                in[i].~TChannel();
            }
            ::operator delete[](in);
            in = nullptr;
        }

        size_t _num_inputs;
        size_t _buffer_size;
        T*  _tmp_buffer = nullptr;
        T*  _sum_buffer = nullptr;
};