#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include <random>
#include <type_traits>
#include <new>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename AWGNKernel<T>::scalar_type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _kernel(noise_stddev) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~NoiseAWGNBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _kernel(_buffer[i]);
        }

        out->writeN(_buffer, transferable);

        return cler::Empty{};
    }

    T processOne(T x) { return _kernel(x); }

private:
    AWGNKernel<T> _kernel;

    T* _buffer;
    size_t _buffer_size;
};
