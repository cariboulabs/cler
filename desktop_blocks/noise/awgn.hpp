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

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _noise_stddev(noise_stddev) { // Default 4096 for dbf compatibility

        std::random_device rd;
        _rng.seed(rd());
        _normal_dist = std::normal_distribution<scalar_type>(0.0, _noise_stddev);
    }

    ~NoiseAWGNBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        size_t to_process = std::min(read_size, write_size);
        
        for (size_t i = 0; i < to_process; ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
                auto n_re = _normal_dist(_rng);
                auto n_im = _normal_dist(_rng);
                write_ptr[i] = read_ptr[i] + T{n_re, n_im};
            } else {
                write_ptr[i] = read_ptr[i] + _normal_dist(_rng);
            }
        }
        
        in.commit_read(to_process);
        out->commit_write(to_process);
        
        return cler::Empty{};
    }

private:
    scalar_type _noise_stddev;

    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;
};
