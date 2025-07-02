#pragma once
#include "cler.hpp"
#include <cmath>
#include <complex>
#include <type_traits>

template <typename T>
struct SourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceCWBlock only supports float or std::complex<float>");

    SourceCWBlock(const char* name, float ampltiude, int frequency_hz, int sps, size_t work_size)
        : cler::BlockBase(name),
        _amplitude(ampltiude),
        _frequency_hz(frequency_hz),
        _sps(sps),
        _work_size(work_size)
    {
        if (_work_size == 0 || _sps == 0) {
            throw std::invalid_argument("Work size and sample rate must be greater than zero.");
        }
        if (_frequency_hz < 0) {
            throw std::invalid_argument("Frequency must be non-negative.");
        }

        _tmp = new T[_work_size];
    }

    ~SourceCWBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
        if (out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        static float phase = 0.0f;
        const float phase_increment = 2.0f * M_PI * _frequency_hz / _sps;

        for (size_t i = 0; i < _work_size; ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _tmp[i] = _amplitude * std::polar(1.0f, phase);
            } else {
                _tmp[i] = _amplitude * std::cos(phase);
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
    T* _tmp;
    float _amplitude;
    int _frequency_hz;
    int _sps;

    size_t _work_size;
};