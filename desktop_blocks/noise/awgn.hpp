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

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _noise_stddev(noise_stddev), _buffer_size(buffer_size) {
        _tmp = new T[buffer_size];

        std::random_device rd;
        _rng.seed(rd());
        _normal_dist = std::normal_distribution<scalar_type>(0.0, _noise_stddev);
    }

    ~NoiseAWGNBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Try zero-copy path first (for doubly mapped buffers)
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr && read_size > 0) {
            auto [write_ptr, write_size] = out->write_dbf();
            if (write_ptr && write_size > 0) {
                // FAST PATH: Process directly between doubly-mapped buffers
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
        }

        // Fall back to standard approach
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t transferable = std::min({available_space, available_samples, _buffer_size});

        size_t read = in.readN(_tmp, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
                auto n_re = _normal_dist(_rng);
                auto n_im = _normal_dist(_rng);
                _tmp[i] += T{n_re, n_im};
            } else {
                _tmp[i] += _normal_dist(_rng);
            }
        }

        size_t written = out->writeN(_tmp, transferable);

        return cler::Empty{};
    }

private:
    scalar_type _noise_stddev;
    size_t _buffer_size;
    T* _tmp;

    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;
};
