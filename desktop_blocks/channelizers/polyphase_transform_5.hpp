#pragma once

#include <array>
#include <complex>
#include <cstddef>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace polyphase5 {

constexpr size_t channels = 5;
constexpr size_t taps_per_subfilter = 6;

inline void winograd_dft(const std::array<float, channels>& bins_r,
                         const std::array<float, channels>& bins_i,
                         std::complex<float>* const* out_channels,
                         size_t frame_index)
{
    constexpr float cos_mean = -0.25f;
    constexpr float cos_half_difference = 0.559016994374947f;
    constexpr float sin_first = 0.951056516295154f;
    constexpr float sin_second = 0.587785252292473f;

    const float sum_outer_r = bins_r[1] + bins_r[4], sum_outer_i = bins_i[1] + bins_i[4];
    const float diff_outer_r = bins_r[1] - bins_r[4], diff_outer_i = bins_i[1] - bins_i[4];
    const float sum_inner_r = bins_r[2] + bins_r[3], sum_inner_i = bins_i[2] + bins_i[3];
    const float diff_inner_r = bins_r[2] - bins_r[3], diff_inner_i = bins_i[2] - bins_i[3];

    const float total_r = sum_outer_r + sum_inner_r, total_i = sum_outer_i + sum_inner_i;
    const float imbalance_r = sum_outer_r - sum_inner_r, imbalance_i = sum_outer_i - sum_inner_i;

    const float mean_r = bins_r[0] + cos_mean * total_r;
    const float mean_i = bins_i[0] + cos_mean * total_i;
    const float spread_r = cos_half_difference * imbalance_r;
    const float spread_i = cos_half_difference * imbalance_i;

    const float near_r = mean_r + spread_r, near_i = mean_i + spread_i;
    const float far_r = mean_r - spread_r, far_i = mean_i - spread_i;

    const float near_quad_r = sin_first * diff_outer_r + sin_second * diff_inner_r;
    const float near_quad_i = sin_first * diff_outer_i + sin_second * diff_inner_i;
    const float far_quad_r = sin_second * diff_outer_r - sin_first * diff_inner_r;
    const float far_quad_i = sin_second * diff_outer_i - sin_first * diff_inner_i;

    out_channels[0][frame_index] = std::complex<float>(bins_r[0] + total_r, bins_i[0] + total_i);
    out_channels[1][frame_index] = std::complex<float>(near_r + near_quad_i, near_i - near_quad_r);
    out_channels[2][frame_index] = std::complex<float>(far_r + far_quad_i, far_i - far_quad_r);
    out_channels[3][frame_index] = std::complex<float>(far_r - far_quad_i, far_i + far_quad_r);
    out_channels[4][frame_index] = std::complex<float>(near_r - near_quad_i, near_i + near_quad_r);
}

#if defined(__ARM_NEON)

inline void transform(const float* taps,
                      const std::complex<float>* window,
                      std::complex<float>* const* out_channels,
                      size_t frame_index)
{
    const float* w = reinterpret_cast<const float*>(window);

    float32x4x2_t s = vld2q_f32(w);
    float32x4_t tap4 = vld1q_f32(taps);
    float32x4_t acc_r4 = vmulq_f32(s.val[0], tap4);
    float32x4_t acc_i4 = vmulq_f32(s.val[1], tap4);
    float acc_r1 = w[8] * taps[4];
    float acc_i1 = w[9] * taps[4];

    for (size_t t = 1; t < taps_per_subfilter; ++t) {
        const float* row = w + t * 2 * channels;
        const float* tap_row = taps + t * channels;
        s = vld2q_f32(row);
        tap4 = vld1q_f32(tap_row);
        acc_r4 = vmlaq_f32(acc_r4, s.val[0], tap4);
        acc_i4 = vmlaq_f32(acc_i4, s.val[1], tap4);
        acc_r1 += row[8] * tap_row[4];
        acc_i1 += row[9] * tap_row[4];
    }

    std::array<float, channels> bins_r;
    std::array<float, channels> bins_i;
    vst1q_f32(bins_r.data(), acc_r4);
    vst1q_f32(bins_i.data(), acc_i4);
    bins_r[4] = acc_r1;
    bins_i[4] = acc_i1;

    winograd_dft(bins_r, bins_i, out_channels, frame_index);
}

#else

inline void transform(const float* taps,
                      const std::complex<float>* window,
                      std::complex<float>* const* out_channels,
                      size_t frame_index)
{
    std::array<float, channels> bins_r;
    std::array<float, channels> bins_i;

    for (size_t k = 0; k < channels; ++k) {
        bins_r[k] = window[k].real() * taps[k];
        bins_i[k] = window[k].imag() * taps[k];
    }
    for (size_t t = 1; t < taps_per_subfilter; ++t) {
        const float* tap_row = taps + t * channels;
        const std::complex<float>* row = window + t * channels;
        for (size_t k = 0; k < channels; ++k) {
            bins_r[k] += row[k].real() * tap_row[k];
            bins_i[k] += row[k].imag() * tap_row[k];
        }
    }

    winograd_dft(bins_r, bins_i, out_channels, frame_index);
}

#endif

}
