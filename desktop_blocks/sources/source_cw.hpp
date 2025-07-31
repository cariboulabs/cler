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
                  size_t buffer_size = 1024)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps)
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }

        float phase_increment = 2.0f * cler::PI * _frequency_hz / static_cast<float>(_sps);

        _phasor = std::complex<float>(1.0f, 0.0f);
        _phasor_inc = std::polar(1.0f, phase_increment);

    }

    ~SourceCWBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        
        if (write_size > 0) {
            // Generate directly into output buffer
            for (size_t i = 0; i < write_size; ++i) {
                std::complex<float> cw = _phasor;

                if constexpr (std::is_same_v<T, std::complex<float>>) {
                    write_ptr[i] = _amplitude * cw;
                } else {
                    write_ptr[i] = _amplitude * cw.real();
                }

                _phasor *= _phasor_inc;
                _phasor /= std::abs(_phasor); // Normalize to keep phasor on the unit circle, CRUCIAL for stability
            }
            
            out->commit_write(write_size);
        }
        return cler::Empty{};
    }

private:
    float _amplitude;
    float _frequency_hz;
    size_t _sps;

    // Recursive oscillator state
    std::complex<float> _phasor = {1.0f, 0.0f};
    std::complex<float> _phasor_inc = {1.0f, 0.0f};

};
