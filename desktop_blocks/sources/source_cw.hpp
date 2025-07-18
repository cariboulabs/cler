#pragma once
#include "cler.hpp"
#include <cmath>
#include <type_traits>
#include <complex>

template <typename T>
struct SourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceCWBlock only supports float or std::complex<float>");

    SourceCWBlock(const char* name,
                  float amplitude,
                  float frequency_hz,
                  size_t sps,
                  size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps),
          _buffer_size(buffer_size)
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }

        float phase_increment = 2.0f * cler::PI * _frequency_hz / static_cast<float>(_sps);

        _phasor = std::complex<float>(1.0f, 0.0f);
        _phasor_inc = std::polar(1.0f, phase_increment);

        _tmp = new T[_buffer_size];
    }

    ~SourceCWBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t available_space = std::min(out->space(), _buffer_size);
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < available_space; ++i) {
            std::complex<float> cw = _phasor;

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _tmp[i] = _amplitude * cw;
            } else {
                _tmp[i] = _amplitude * cw.real();
            }

            _phasor *= _phasor_inc;
            _phasor /= std::abs(_phasor); // Normalize to keep phasor on the unit circle, CRUCIAL for stability
        }

        out->writeN(_tmp, available_space);

        return cler::Empty{};
    }

private:
    float _amplitude;
    float _frequency_hz;
    size_t _sps;
    size_t _buffer_size;

    // Recursive oscillator state
    std::complex<float> _phasor = {1.0f, 0.0f};
    std::complex<float> _phasor_inc = {1.0f, 0.0f};

    T* _tmp = nullptr;
};
