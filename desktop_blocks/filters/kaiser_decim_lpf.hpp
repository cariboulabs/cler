#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <type_traits>
#include <cmath>
#include <cassert>

template <typename T>
struct KaiserDecimLPFBlock : public cler::BlockBase {
    cler::Channel<T> in;

    // Kaiser decimating low-pass filter using liquid-dsp
    // Combines filtering and downsampling in one efficient operation
    //
    // Parameters:
    //   sample_rate        : Input sample rate in Hz (e.g., 1e6 for 1 MSPS)
    //   cutoff_freq        : Cutoff frequency in Hz (e.g., 15e3 for 15 kHz)
    //   transition_bw      : Transition bandwidth in Hz (e.g., 5e3 for 5 kHz)
    //   attenuation_db     : Stopband attenuation in dB (default: 60)
    //   decimation_factor  : Downsampling factor (e.g., 5 reduces 1 MSPS to 200 kSPS)
    //
    // Output sample rate = sample_rate / decimation_factor
    KaiserDecimLPFBlock(const char* name,
                        double sample_rate,
                        double cutoff_freq,
                        double transition_bw,
                        unsigned int decimation_factor,
                        float attenuation_db = 60.0,
                        size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _sample_rate(sample_rate),
          _cutoff_freq(cutoff_freq),
          _decimation_factor(decimation_factor)
    {
        // Validate buffer size
        assert(buffer_size == 0 || buffer_size * sizeof(T) >= cler::DOUBLY_MAPPED_MIN_SIZE);

        // Validate parameters
        assert(sample_rate > 0.0 && "Sample rate must be positive");
        assert(transition_bw > 0.0 && "Transition bandwidth must be positive");
        assert(attenuation_db > 0.0 && "Attenuation must be positive");
        assert(decimation_factor >= 2 && "Decimation factor must be >= 2");

        float fc;
        if constexpr (std::is_same_v<T, std::complex<float>>) {
            assert(cutoff_freq > 0.0 && cutoff_freq < sample_rate &&
                   "Cutoff frequency must be between 0 and sample_rate for complex signals");
            fc = static_cast<float>(cutoff_freq / sample_rate);
            assert(fc < 1.0f && "Cutoff frequency must be less than sample_rate for complex signals");
        } else {
            assert(cutoff_freq > 0.0 && cutoff_freq < sample_rate / 2.0 &&
                   "Cutoff frequency must be between 0 and Nyquist (sample_rate/2) for real signals");
            fc = static_cast<float>(cutoff_freq / sample_rate);
            assert(fc < 0.5f && "Cutoff frequency must be less than Nyquist frequency for real signals");
        }

        // Calculate filter delay parameter (m) based on decimation factor
        // liquid-dsp recommendation: m >= 2 for good performance
        unsigned int m = 3;

        // Calculate number of filter taps
        // For decimators: num_taps = 2 * m * decimation_factor + 1
        unsigned int num_taps = 2 * m * decimation_factor + 1;

        // Design Kaiser window filter coefficients
        float* filter_taps = new float[num_taps];
        liquid_firdes_kaiser(
            num_taps,
            fc,
            attenuation_db,
            0.0f,  // mu (fractional sample delay)
            filter_taps
        );

        // Create decimating filter
        if constexpr (std::is_same_v<T, float>) {
            _decim_r = firdecim_rrrf_create(decimation_factor, filter_taps, num_taps);
            assert(_decim_r && "Failed to create Kaiser decimating LPF for float");
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _decim_c = firdecim_crcf_create(decimation_factor, filter_taps, num_taps);
            assert(_decim_c && "Failed to create Kaiser decimating LPF for complex float");
        }

        delete[] filter_taps;
    }

    ~KaiserDecimLPFBlock() {
        if constexpr (std::is_same_v<T, float>) {
            if (_decim_r) {
                firdecim_rrrf_destroy(_decim_r);
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            if (_decim_c) {
                firdecim_crcf_destroy(_decim_c);
            }
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Input samples must be a multiple of decimation factor
        size_t input_frames = read_size / _decimation_factor;
        if (input_frames == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Output frames limited by available space
        size_t output_frames = std::min(input_frames, write_space);
        size_t input_samples = output_frames * _decimation_factor;

        // Execute decimating filter block
        if constexpr (std::is_same_v<T, float>) {
            firdecim_rrrf_execute_block(
                _decim_r,
                const_cast<float*>(read_ptr),
                input_samples,
                write_ptr
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            firdecim_crcf_execute_block(
                _decim_c,
                reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)),
                input_samples,
                reinterpret_cast<liquid_float_complex*>(write_ptr)
            );
        }

        in.commit_read(input_samples);
        out->commit_write(output_frames);

        return cler::Empty{};
    }

    // Getters for info
    double get_input_sample_rate() const { return _sample_rate; }
    double get_output_sample_rate() const { return _sample_rate / _decimation_factor; }
    double get_cutoff_freq() const { return _cutoff_freq; }
    unsigned int get_decimation_factor() const { return _decimation_factor; }

private:
    firdecim_rrrf _decim_r = nullptr;
    firdecim_crcf _decim_c = nullptr;
    double _sample_rate;
    double _cutoff_freq;
    unsigned int _decimation_factor;
};
