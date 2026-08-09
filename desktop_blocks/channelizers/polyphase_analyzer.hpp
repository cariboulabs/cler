#pragma once

#include "liquid.h"
#include "polyphase_transform_5.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>

template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
class PolyphaseAnalyzer {
    static_assert(NUM_CHANNELS > 0, "Number of channels must be positive");
    static_assert(FILTER_SEMILENGTH > 0, "Filter semilength must be positive");

public:
    static constexpr size_t channels = NUM_CHANNELS;
    static constexpr size_t taps_per_subfilter = 2 * FILTER_SEMILENGTH;
    static constexpr size_t history_frames = taps_per_subfilter - 1;

    explicit PolyphaseAnalyzer(float stopband_attenuation_db)
    {
        design_folded_taps(stopband_attenuation_db);
        build_twiddles();
    }

    void execute(const std::complex<float>* frames,
                 size_t num_frames,
                 std::complex<float>* const* out_channels)
    {
        const size_t from_carry = std::min(history_frames, num_frames);
        constexpr size_t frame_bytes = channels * sizeof(std::complex<float>);

        std::memcpy(_carry.data() + history_frames * channels, frames, from_carry * frame_bytes);

        for (size_t j = 0; j < from_carry; ++j) {
            transform(_carry.data() + j * channels, out_channels, j);
        }
        for (size_t j = history_frames; j < num_frames; ++j) {
            transform(frames + (j - history_frames) * channels, out_channels, j);
        }

        if (num_frames >= history_frames) {
            std::memcpy(_carry.data(), frames + (num_frames - history_frames) * channels,
                        history_frames * frame_bytes);
        } else {
            std::memmove(_carry.data(), _carry.data() + num_frames * channels,
                         (history_frames - num_frames) * frame_bytes);
            std::memcpy(_carry.data() + (history_frames - num_frames) * channels, frames,
                        num_frames * frame_bytes);
        }
    }

private:
    static constexpr bool uses_codelet = (channels == polyphase5::channels &&
                                          taps_per_subfilter == polyphase5::taps_per_subfilter);
    static constexpr size_t folded_taps_len = channels * taps_per_subfilter;
    static constexpr size_t twiddle_len = uses_codelet ? 0 : channels;

    void design_folded_taps(float stopband_attenuation_db)
    {
        std::array<float, folded_taps_len + 1> prototype{};
        const float cutoff = 0.5f / static_cast<float>(channels);
        liquid_firdes_kaiser(static_cast<unsigned int>(prototype.size()), cutoff,
                             std::fabs(stopband_attenuation_db), 0.0f, prototype.data());
        std::reverse_copy(prototype.begin(), prototype.begin() + folded_taps_len, _taps.begin());
    }

    void build_twiddles()
    {
        const double turn = -2.0 * M_PI / static_cast<double>(channels);
        for (size_t i = 0; i < twiddle_len; ++i) {
            const double angle = turn * static_cast<double>(i);
            _twiddles[i] = std::complex<float>(static_cast<float>(std::cos(angle)),
                                               static_cast<float>(std::sin(angle)));
        }
    }

    inline void transform(const std::complex<float>* window,
                          std::complex<float>* const* out_channels,
                          size_t frame_index) const
    {
        if constexpr (uses_codelet) {
            polyphase5::transform(_taps.data(), window, out_channels, frame_index);
        } else {
            std::array<float, channels> bins_r;
            std::array<float, channels> bins_i;

            for (size_t k = 0; k < channels; ++k) {
                bins_r[k] = window[k].real() * _taps[k];
                bins_i[k] = window[k].imag() * _taps[k];
            }
            for (size_t t = 1; t < taps_per_subfilter; ++t) {
                const float* tap_row = _taps.data() + t * channels;
                const std::complex<float>* row = window + t * channels;
                for (size_t k = 0; k < channels; ++k) {
                    bins_r[k] += row[k].real() * tap_row[k];
                    bins_i[k] += row[k].imag() * tap_row[k];
                }
            }

            for (size_t k = 0; k < channels; ++k) {
                float acc_r = bins_r[0];
                float acc_i = bins_i[0];
                for (size_t n = 1; n < channels; ++n) {
                    const std::complex<float> w = _twiddles[(k * n) % channels];
                    acc_r += bins_r[n] * w.real() - bins_i[n] * w.imag();
                    acc_i += bins_r[n] * w.imag() + bins_i[n] * w.real();
                }
                out_channels[k][frame_index] = std::complex<float>(acc_r, acc_i);
            }
        }
    }

    std::array<float, folded_taps_len> _taps{};
    std::array<std::complex<float>, 2 * history_frames * channels> _carry{};
    std::array<std::complex<float>, twiddle_len> _twiddles{};
};
