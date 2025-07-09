#pragma once
#include "cler.hpp"
#include <cmath>
#include <numbers>
#include <type_traits>

template <typename T>
struct SourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceCWBlock only supports float or std::complex<float>");

    SourceCWBlock(std::string name, float amplitude, float frequency_hz, size_t sps)
        : cler::BlockBase(std::move(name)),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps)
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        const float phase_increment = 2.0f * PI * _frequency_hz / static_cast<float>(_sps);

        for (size_t i = 0; i < cler::floor2(out->space()); ++i) {
            // Always generate complex exponential
            std::complex<float> cw = _amplitude * std::polar(1.0f, _phase);

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                out->push(cw);
            } else {
                out->push(cw.real());
            }

            _phase += phase_increment;

            // Keep phase bounded for numerical stability
            if (_phase >= 2.0f * PI) {
                _phase -= 2.0f * PI;
            } else if (_phase <= -2.0f * PI) {
                _phase += 2.0f * PI;
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
};
