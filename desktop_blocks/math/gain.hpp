#include "cler.hpp"

//a one to one gain block over arbitrary types
template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, const T gain_value, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _gain(gain_value) {
        
        // Allocate temporary buffer for readN/writeN operations
        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new T[_buffer_size];
        if (!_buffer) {
            throw std::bad_alloc();
        }
    }
    
    ~GainBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use readN/writeN for simple processing (recommended pattern)
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        in.readN(_buffer, transferable);
        
        // Process buffer
        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _buffer[i] * _gain;
        }
        
        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

    private:
        T _gain;
        T* _buffer;
        size_t _buffer_size;
};