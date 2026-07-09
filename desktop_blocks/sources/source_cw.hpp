#pragma once
#include "cler.hpp"
#include "cler_utils.hpp"
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
                  size_t sps)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps)
    {
        if (_sps == 0) {
            cler::panic("Sample rate must be greater than zero.");
        }

        float phase_increment = 2.0f * cler::PI * _frequency_hz / static_cast<float>(_sps);

        _phasor = std::complex<float>(1.0f, 0.0f);
        _phasor_inc = std::polar(1.0f, phase_increment);

        _buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T);
        _buffer = new T[_buffer_size];
    }

    ~SourceCWBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t to_generate = std::min(out->space(), _buffer_size);
        if (to_generate == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < to_generate; ++i) {
            std::complex<float> cw = _phasor;

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _buffer[i] = _amplitude * cw;
            } else {
                _buffer[i] = _amplitude * cw.real();
            }

            _phasor *= _phasor_inc;
            _phasor /= std::abs(_phasor); // renormalize to unit circle each step, required for numerical stability
        }

        out->writeN(_buffer, to_generate);
        return cler::Empty{};
    }

private:
    float _amplitude;
    float _frequency_hz;
    size_t _sps;

    std::complex<float> _phasor = {1.0f, 0.0f};
    std::complex<float> _phasor_inc = {1.0f, 0.0f};

    T* _buffer;
    size_t _buffer_size;
};
