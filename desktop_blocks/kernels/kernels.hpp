#pragma once

#include <complex>
#include <random>
#include <type_traits>

template<typename T>
struct GainKernel {
    T gain;
    T operator()(T x) const { return x * gain; }
};

template<typename T>
struct AWGNKernel {
    using scalar_type = typename std::conditional<
        std::is_same_v<T, std::complex<float>>, float,
        typename std::conditional<std::is_same_v<T, std::complex<double>>, double, T>::type>::type;

    // seed 0 draws one from random_device; anything else is reproducible, which
    // is what a test asserting on the statistics needs
    explicit AWGNKernel(scalar_type noise_stddev, uint32_t seed = 0)
        : _normal_dist(0.0, noise_stddev) {
        if (seed == 0) {
            std::random_device rd;
            _rng.seed(rd());
        } else {
            _rng.seed(seed);
        }
    }

    void set_stddev(scalar_type stddev) {
        _normal_dist = std::normal_distribution<scalar_type>(0.0, stddev);
    }

    T operator()(T x) {
        if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
            auto n_re = _normal_dist(_rng);
            auto n_im = _normal_dist(_rng);
            return x + T{n_re, n_im};
        } else {
            return x + _normal_dist(_rng);
        }
    }

private:
    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;
};
