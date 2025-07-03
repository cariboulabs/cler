#pragma once
#include "cler.hpp"
#include <cmath>
#include <complex>
#include <type_traits>

template <typename T>
struct SourceChirpBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceChirpBlock only supports float or std::complex<float>");

    SourceChirpBlock(const char* name,
                     float amplitude,
                     float f0_hz,
                     float f1_hz,
                     size_t sps,
                     float chirp_duration_s,
                     size_t work_size)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _f0(f0_hz),
          _f1(f1_hz),
          _sps(sps),
          _work_size(work_size)
    {
        if (_work_size == 0 || _sps == 0 || chirp_duration_s == 0) {
            throw std::invalid_argument("Work size, sample rate, and chirp duration must be greater than zero.");
        }

        _chirp_duration_samples = static_cast<size_t>(std::round(static_cast<double>(_sps) * chirp_duration_s));
        _k = (_f1 - _f0) /static_cast<float>(_chirp_duration_samples);

        _tmp = new T[_work_size];
    }

    ~SourceChirpBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
        if (out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < _work_size; ++i) {
            float current_time = static_cast<float>(_current_sample_idx);
            float instant_freq = _f0 + _k * current_time;

            _phase += 2.0f * M_PI * instant_freq / _sps;

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _tmp[i] = _amplitude * std::polar(1.0f, _phase);
            } else {
                _tmp[i] = _amplitude * std::cos(_phase);
            }

            ++_current_sample_idx;

            if (_current_sample_idx >= _chirp_duration_samples) {
                _current_sample_idx = 0;
                _phase = 0.0f;  // restart chirp
            }
        }

        out->writeN(_tmp, _work_size);

        return cler::Empty{};
    }

private:
    T* _tmp;
    float _amplitude;

    float _f0;
    float _f1;
    float _k; // chirp rate (Hz/sample)
    size_t _sps;
    size_t _chirp_duration_samples;

    size_t _current_sample_idx = 0;
    float _phase = 0.0f;

    size_t _work_size;
};
