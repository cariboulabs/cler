#pragma once
#include "cler.hpp"
#include <cmath>
#include <complex>
#include <numbers>
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
                     float chirp_duration_s)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _f0_hz(f0_hz),
          _f1_hz(f1_hz),
          _sps(sps),
          _chirp_duration_s(chirp_duration_s)
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }
        if (_chirp_duration_s <= 0) {
            throw std::invalid_argument("Chirp duration must be positive.");
        }

        _num_samples = static_cast<size_t>(_chirp_duration_s * _sps);
        _k = (_f1_hz - _f0_hz) / _chirp_duration_s; // Hz/s sweep rate
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        const size_t n_samples = cler::floor2(out->space());

        for (size_t i = 0; i < n_samples; ++i) {
            float t = static_cast<float>(_sample_idx) / static_cast<float>(_sps);

            // Instantaneous frequency f(t)
            float instant_freq = _f0_hz + _k * t;

            // Increment phase
            _phase += 2.0f * PI * instant_freq / static_cast<float>(_sps);

            // Keep phase bounded
            if (_phase >= 2.0f * PI) {
                _phase -= 2.0f * PI;
            } else if (_phase <= -2.0f * PI) {
                _phase += 2.0f * PI;
            }

            // Always generate complex
            std::complex<float> chirp = _amplitude * std::polar(1.0f, _phase);

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                out->push(chirp);
            } else {
                out->push(chirp.real());
            }

            _sample_idx++;
            if (_sample_idx >= _num_samples) {
                _sample_idx = 0;  // loop chirp if desired
            }
        }

        return cler::Empty{};
    }

private:
    static constexpr float PI = std::numbers::pi_v<float>;
    float _amplitude;
    float _f0_hz;
    float _f1_hz;
    size_t _sps;
    float _chirp_duration_s;

    size_t _num_samples;
    float _k;              // Sweep rate (Hz/s)
    size_t _sample_idx = 0;
    float _phase = 0.0f;
};
