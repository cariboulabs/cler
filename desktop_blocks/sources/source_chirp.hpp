#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
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
                    float chirp_duration_s)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _f0_hz(f0_hz),
          _f1_hz(f1_hz),
          _sps(sps),
          _chirp_duration_s(chirp_duration_s)
    {
        if (_sps == 0) cler::panic("Sample rate must be greater than zero.");
        if (_chirp_duration_s <= 0) cler::panic("Chirp duration must be positive.");

        _n_samples_before_reset = static_cast<size_t>(_chirp_duration_s * _sps);
        _k = (_f1_hz - _f0_hz) / _chirp_duration_s; // Hz/s

        const double dt = 1.0 / static_cast<double>(_sps);
        const double w0 = 2.0 * cler::PI * static_cast<double>(_f0_hz) * dt;

        _psi = std::polar(1.0, w0);
        _psi_inc = std::polar(1.0, 2.0 * cler::PI * static_cast<double>(_k) * dt * dt); // second-difference recursion for phase acceleration

        _phasor = std::complex<double>(1.0, 0.0);
    }

    ~SourceChirpBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < write_size; ++i) {
            std::complex<float> chirp(static_cast<float>(_phasor.real()),
                                      static_cast<float>(_phasor.imag()));

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                write_ptr[i] = _amplitude * chirp;
            } else {
                write_ptr[i] = _amplitude * chirp.real();
            }

            _phasor *= _psi;
            _phasor /= std::abs(_phasor); // renormalize to unit circle each step, required for numerical stability
            _psi *= _psi_inc;

            ++_samples_counter;
            if (_samples_counter >= _n_samples_before_reset) {
                reset();
            }
        }

        out->commit_write(write_size);
        return cler::Empty{};
    }

private:
    void reset() {
        _samples_counter = 0;
        _phasor = std::complex<double>(1.0, 0.0);
        const double dt = 1.0 / static_cast<double>(_sps);
        const double w0 = 2.0 * cler::PI * static_cast<double>(_f0_hz) * dt;
        _psi = std::polar(1.0, w0);
        _psi_inc = std::polar(1.0, 2.0 * cler::PI * static_cast<double>(_k) * dt * dt);
    }

    float _amplitude;
    float _f0_hz;
    float _f1_hz;
    size_t _sps;
    float _chirp_duration_s;

    size_t _n_samples_before_reset;
    float _k;              // sweep rate, Hz/s
    size_t _samples_counter = 0;

    std::complex<double> _phasor;
    std::complex<double> _psi;
    std::complex<double> _psi_inc;
};
