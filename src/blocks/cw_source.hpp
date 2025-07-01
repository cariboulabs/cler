#pragma once
#include "cler.hpp"
#include <cmath>
#include <complex>
#include <type_traits>

template <typename T>
struct CWSourceBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "CWSourceBlock only supports float or std::complex<float>");

    CWSourceBlock(const char* name, int frequency_hz, int sample_rate_sps, size_t work_size)
        : cler::BlockBase(name),
          _work_size(work_size),
          _frequency_hz(frequency_hz),
          _sample_rate_sps(sample_rate_sps)
    {
        if (_work_size == 0 || _sample_rate_sps == 0) {
            throw std::invalid_argument("Work size and sample rate must be greater than zero.");
        }
        if (_frequency_hz < 0) {
            throw std::invalid_argument("Frequency must be non-negative.");
        }

        _tmp = new T[_work_size];
    }

    ~CWSourceBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
        if (out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        static float phase = 0.0f;
        const float phase_increment = 2.0f * M_PI * _frequency_hz / _sample_rate_sps;

        for (size_t i = 0; i < _work_size; ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _tmp[i] = std::polar(1.0f, phase);
            } else {
                _tmp[i] = std::cos(phase);
            }

            phase += phase_increment;
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }
        out->writeN(_tmp, _work_size);

        return cler::Empty{};
    }

private:
    size_t _work_size;
    T* _tmp;
    int _frequency_hz;
    int _sample_rate_sps;
};