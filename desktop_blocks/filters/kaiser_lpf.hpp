#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <type_traits>
#include <cmath>

template <typename T>
struct KaiserLPFBlock : public cler::BlockBase {
    cler::Channel<T> in;

    // Kaiser low-pass filter using liquid-dsp
    // Parameters:
    //   sample_rate      : Input sample rate in Hz (e.g., 2e6 for 2 MSPS)
    //   cutoff_freq      : Cutoff frequency in Hz (e.g., 100e3 for 100 kHz)
    //   transition_bw    : Transition bandwidth in Hz (e.g., 20e3 for 20 kHz)
    //   attenuation_db   : Stopband attenuation in dB (default: 60)
    KaiserLPFBlock(const char* name,
                   double sample_rate,
                   double cutoff_freq,
                   double transition_bw,
                   double attenuation_db = 60.0,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _sample_rate(sample_rate),
          _cutoff_freq(cutoff_freq)
    {
        // Validate buffer size
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument(
                "Buffer size too small for doubly-mapped buffers. Need at least " +
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) +
                " elements of type T");
        }

        // Validate parameters
        if (sample_rate <= 0.0) {
            throw std::invalid_argument("Sample rate must be positive");
        }
        if (cutoff_freq <= 0.0 || cutoff_freq >= sample_rate / 2.0) {
            throw std::invalid_argument("Cutoff frequency must be between 0 and Nyquist");
        }
        if (transition_bw <= 0.0) {
            throw std::invalid_argument("Transition bandwidth must be positive");
        }
        if (attenuation_db <= 0.0) {
            throw std::invalid_argument("Attenuation must be positive");
        }

        // Normalize cutoff frequency to [0, 0.5] where 0.5 is Nyquist
        float fc = static_cast<float>(cutoff_freq / sample_rate);
        if (fc >= 0.5f) {
            throw std::invalid_argument("Cutoff frequency must be less than Nyquist frequency (sample_rate/2)");
        }

        // Calculate filter order based on transition bandwidth and attenuation
        // liquid-dsp uses: order = ceil(attenuation / (22.0 * transition_bw_normalized))
        float transition_bw_normalized = static_cast<float>(transition_bw / sample_rate);
        unsigned int filter_order = static_cast<unsigned int>(
            std::ceil(attenuation_db / (22.0f * transition_bw_normalized)));

        if (filter_order < 5) {
            throw std::invalid_argument(
                "Filter order too small. Increase transition_bw or decrease attenuation_db");
        }
        // Ensure odd order for better frequency response
        if (filter_order % 2 == 0) {
            filter_order++;
        }

        // Create Kaiser low-pass filter using liquid-dsp
        if constexpr (std::is_same_v<T, float>) {
            _filter_r = firfilt_rrrf_create_kaiser(
                filter_order,
                fc,
                static_cast<float>(attenuation_db),
                0.0f);  // mu (fractional sample delay, usually 0)

            if (!_filter_r) {
                throw std::runtime_error("Failed to create Kaiser LPF for float");
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _filter_c = firfilt_crcf_create_kaiser(
                filter_order,
                fc,
                static_cast<float>(attenuation_db),
                0.0f);  // mu (fractional sample delay, usually 0)

            if (!_filter_c) {
                throw std::runtime_error("Failed to create Kaiser LPF for complex float");
            }
        }
    }

    ~KaiserLPFBlock() {
        if constexpr (std::is_same_v<T, float>) {
            if (_filter_r) {
                firfilt_rrrf_destroy(_filter_r);
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            if (_filter_c) {
                firfilt_crcf_destroy(_filter_c);
            }
        }
    }

    // Simple: T -> T (same as resampler pattern)
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t samples_to_process = std::min(read_size, write_space);

        if constexpr (std::is_same_v<T, float>) {
            firfilt_rrrf_execute_block(
                _filter_r,
                const_cast<float*>(read_ptr),
                samples_to_process,
                write_ptr
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            firfilt_crcf_execute_block(
                _filter_c,
                reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)),
                samples_to_process,
                reinterpret_cast<liquid_float_complex*>(write_ptr)
            );
        }

        in.commit_read(samples_to_process);
        out->commit_write(samples_to_process);

        return cler::Empty{};
    }

private:
    firfilt_rrrf _filter_r = nullptr;
    firfilt_crcf _filter_c = nullptr;
    double _sample_rate;
    double _cutoff_freq;
};
