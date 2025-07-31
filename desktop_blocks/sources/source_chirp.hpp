#pragma once
#include "cler.hpp"
#include <cmath>
#include <type_traits>
#include <complex>

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
                    size_t buffer_size = 1024)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _f0_hz(f0_hz),
          _f1_hz(f1_hz),
          _sps(sps),
          _chirp_duration_s(chirp_duration_s)
    {
        if (_sps == 0) throw std::invalid_argument("Sample rate must be greater than zero.");
        if (_chirp_duration_s <= 0) throw std::invalid_argument("Chirp duration must be positive.");

        _n_samples_before_reset = static_cast<size_t>(_chirp_duration_s * _sps);
        _k = (_f1_hz - _f0_hz) / _chirp_duration_s; // Hz/s

        // Precompute per-sample sweep rate
        const float dt = 1.0f / static_cast<float>(_sps);
        const float w0 = 2.0f * cler::PI * _f0_hz * dt;

        // Precompute second difference method constants
        _psi = std::polar(1.0f, w0);      // Initial phasor increment
        _psi_inc = std::polar(1.0f, 2.0f * cler::PI * _k * dt * dt); // Second difference (==acceleration)

        _phasor = std::complex<float>(1.0f, 0.0f); // Unit phasor

    }

    ~SourceChirpBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        
        if (write_size > 0) {
            // Generate directly into output buffer
            for (size_t i = 0; i < write_size; ++i) {
                std::complex<float> chirp = _phasor;

                if constexpr (std::is_same_v<T, std::complex<float>>) {
                    write_ptr[i] = _amplitude * chirp;
                } else {
                    write_ptr[i] = _amplitude * chirp.real();
                }

                _phasor *= _psi;
                _phasor /= std::abs(_phasor); // Normalize to keep phasor on the unit circle, CRUCIAL for stability
                _psi *= _psi_inc; // Update phase increment for next sample

                ++_samples_counter;
                if (_samples_counter >= _n_samples_before_reset) {
                    reset();
                }
            }

            out->commit_write(write_size);
        }
        return cler::Empty{};
    }

private:
    void reset() {
        _samples_counter = 0;
        _phasor = std::complex<float>(1.0f, 0.0f);
        const float dt = 1.0f / static_cast<float>(_sps);
        const float w0 = 2.0f * cler::PI * _f0_hz * dt;
        _psi = std::polar(1.0f, w0);
        _psi_inc = std::polar(1.0f, 2.0f * cler::PI * _k * dt * dt);
    }

    float _amplitude;
    float _f0_hz;
    float _f1_hz;
    size_t _sps;
    float _chirp_duration_s;

    size_t _n_samples_before_reset;
    float _k;              // Sweep rate (Hz/s)
    size_t _samples_counter = 0;

    // Recursive oscillator state
    std::complex<float> _phasor;   // Current sample phasor
    std::complex<float> _psi;      // Current phase increment
    std::complex<float> _psi_inc;  // Second difference (sweep)
};
