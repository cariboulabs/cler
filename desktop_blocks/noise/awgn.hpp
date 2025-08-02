#pragma once

#include "cler.hpp"
#include <random>
#include <type_traits>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename std::conditional<
        std::is_same_v<T, std::complex<float>>, float,
        typename std::conditional<std::is_same_v<T, std::complex<double>>, double, T>::type>::type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _noise_stddev(noise_stddev) {
        
        // Allocate temporary buffer for readN/writeN operations
        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new T[_buffer_size];
        if (!_buffer) {
            throw std::bad_alloc();
        }

        std::random_device rd;
        _rng.seed(rd());
        _normal_dist = std::normal_distribution<scalar_type>(0.0, _noise_stddev);
    }

    ~NoiseAWGNBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use readN/writeN for simple processing (recommended pattern)
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        // Read input data
        in.readN(_buffer, transferable);
        
        // Add noise to buffer
        for (size_t i = 0; i < transferable; ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
                auto n_re = _normal_dist(_rng);
                auto n_im = _normal_dist(_rng);
                _buffer[i] = _buffer[i] + T{n_re, n_im};
            } else {
                _buffer[i] = _buffer[i] + _normal_dist(_rng);
            }
        }
        
        // Write output
        out->writeN(_buffer, transferable);
        
        return cler::Empty{};
    }

private:
    scalar_type _noise_stddev;

    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;
    
    // Temporary buffer for readN/writeN
    T* _buffer;
    size_t _buffer_size;
};
