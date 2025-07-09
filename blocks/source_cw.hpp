#pragma once
#include "cler.hpp"
#include <cmath>
#include <complex>
#include <type_traits>

template <typename T>
struct SourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceCWBlock only supports float or std::complex<float>");

    SourceCWBlock(const char* name, const float amplitude, const float frequency_hz, const size_t sps)
        : cler::BlockBase(name),
        _amplitude(amplitude),
        _frequency_hz(frequency_hz),
        _sps(sps)
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        const float phase_increment = 2.0f * PI * _frequency_hz / _sps;

        for (size_t i = 0; i < cler::floor2(out->space()); ++i) {
            if constexpr (std::is_same_v<T, std::complex<float>>) {
                out->push(_amplitude * std::polar(1.0f, _phase));
            } else {
                out->push(_amplitude * std::cos(_phase));
            }

            _phase += phase_increment;
            if (_phase >= 2.0f * PI) {
                _phase -= 2.0f * PI;
            }
        }

        return cler::Empty{};
    }

private:
    static constexpr float PI = std::numbers::pi_v<float>; 
    float _amplitude;
    float _frequency_hz;
    size_t _sps;

    float _phase = 0.0f;
    size_t _work_size;
};