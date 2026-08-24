#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include <atomic>
#include <random>
#include <type_traits>
#include <new>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename AWGNKernel<T>::scalar_type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0, uint32_t seed = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _kernel(noise_stddev, seed) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~NoiseAWGNBlock() {
        delete[] _buffer;
    }

    // Thread-safe: applied at the top of the next procedure().
    void set_noise_stddev(scalar_type stddev) {
        _pending_stddev.store(stddev, std::memory_order_relaxed);
        _stddev_dirty.store(true, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (_stddev_dirty.exchange(false, std::memory_order_acquire)) {
            _kernel.set_stddev(_pending_stddev.load(std::memory_order_relaxed));
        }
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
    std::atomic<scalar_type> _pending_stddev{0};
    std::atomic<bool> _stddev_dirty{false};

    T* _buffer;
    size_t _buffer_size;
};
