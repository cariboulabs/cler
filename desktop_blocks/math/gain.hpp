#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

//a one to one gain block over arbitrary types
template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, const T gain_value, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _gain(gain_value) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~GainBlock() {
        delete[] _buffer;
    }

    // buffer_size isn't validated to be >=4KB (custom sizes allowed), so dbf isn't
    // guaranteed available here; readN/writeN into a temp buffer stays correct for any size.
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _buffer[i] * _gain;
        }

        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

    T processOne(T x) { return x * _gain; }

    private:
        T _gain;
        T* _buffer;
        size_t _buffer_size;
};