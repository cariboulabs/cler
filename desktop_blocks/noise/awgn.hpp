#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <random>
#include <type_traits>
#include <new>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename std::conditional<
        std::is_same_v<T, std::complex<float>>, float,
        typename std::conditional<std::is_same_v<T, std::complex<double>>, double, T>::type>::type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _noise_stddev(noise_stddev) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }

        std::random_device rd;
        _rng.seed(rd());
        _normal_dist = std::normal_distribution<scalar_type>(0.0, _noise_stddev);
    }

    ~NoiseAWGNBlock() {
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
            if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
                auto n_re = _normal_dist(_rng);
                auto n_im = _normal_dist(_rng);
                _buffer[i] = _buffer[i] + T{n_re, n_im};
            } else {
                _buffer[i] = _buffer[i] + _normal_dist(_rng);
            }
        }

        out->writeN(_buffer, transferable);

        return cler::Empty{};
    }

    T processOne(T x) {
        if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
            auto n_re = _normal_dist(_rng);
            auto n_im = _normal_dist(_rng);
            return x + T{n_re, n_im};
        } else {
            return x + _normal_dist(_rng);
        }
    }

private:
    scalar_type _noise_stddev;

    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;

    T* _buffer;
    size_t _buffer_size;
};
