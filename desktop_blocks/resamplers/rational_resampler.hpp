#pragma once

#include "liquid.h"
#include "cler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

template <size_t INTERP, size_t DECIM, size_t TAPS_PER_PHASE>
class RationalResampler {
    static_assert(INTERP > 0, "Interpolation factor must be positive");
    static_assert(DECIM > 0, "Decimation factor must be positive");
    static_assert(TAPS_PER_PHASE > 1, "Need at least two taps per phase");

public:
    static constexpr size_t interp = INTERP;
    static constexpr size_t decim = DECIM;
    static constexpr size_t taps_per_phase = TAPS_PER_PHASE;
    static constexpr size_t history = TAPS_PER_PHASE - 1;

    explicit RationalResampler(float stopband_attenuation_db)
    {
        design_phase_taps(stopband_attenuation_db);
    }

    static constexpr size_t max_outputs(size_t num_input)
    {
        return (num_input * INTERP) / DECIM + 1;
    }

    size_t outputs_for(size_t num_input) const
    {
        size_t phase = _phase;
        size_t needed = _input_until_next_output;
        size_t count = 0;
        while (needed <= num_input) {
            ++count;
            const size_t step = (phase + DECIM) / INTERP;
            phase = (phase + DECIM) % INTERP;
            needed += step;
        }
        return count;
    }

    size_t process(const std::complex<float>* in, size_t num_input, std::complex<float>* out)
    {
        constexpr size_t sample_bytes = sizeof(std::complex<float>);
        const size_t from_carry = std::min(history, num_input);
        std::memcpy(_carry.data() + history, in, from_carry * sample_bytes);

        size_t produced = 0;
        size_t cursor = _input_until_next_output;

        while (cursor <= num_input) {
            const size_t newest = cursor - 1;
            const std::complex<float>* window =
                newest < history ? _carry.data() + newest
                                 : in + (newest - history);
            out[produced++] = filter(window, _phase);

            const size_t step = (_phase + DECIM) / INTERP;
            _phase = (_phase + DECIM) % INTERP;
            cursor += step;
        }
        _input_until_next_output = cursor - num_input;

        if (num_input >= history) {
            std::memcpy(_carry.data(), in + (num_input - history), history * sample_bytes);
        } else {
            std::memmove(_carry.data(), _carry.data() + num_input, (history - num_input) * sample_bytes);
            std::memcpy(_carry.data() + (history - num_input), in, num_input * sample_bytes);
        }
        return produced;
    }

private:
    static constexpr size_t prototype_len = INTERP * TAPS_PER_PHASE;

    void design_phase_taps(float stopband_attenuation_db)
    {
        std::array<float, prototype_len> prototype{};
        const float cutoff = 0.5f / static_cast<float>(std::max(INTERP, DECIM));
        liquid_firdes_kaiser(static_cast<unsigned int>(prototype_len), cutoff,
                             std::fabs(stopband_attenuation_db), 0.0f, prototype.data());

        float dc = 0.0f;
        for (float t : prototype) dc += t;
        const float gain = static_cast<float>(INTERP) / dc;

        for (size_t phase = 0; phase < INTERP; ++phase) {
            for (size_t j = 0; j < TAPS_PER_PHASE; ++j) {
                const size_t tap = (TAPS_PER_PHASE - 1 - j) * INTERP + phase;
                _taps[phase * TAPS_PER_PHASE + j] = gain * prototype[tap];
            }
        }
    }

#if defined(__ARM_NEON)

    inline std::complex<float> filter(const std::complex<float>* window, size_t phase) const
    {
        const float* tap = _taps.data() + phase * TAPS_PER_PHASE;
        const float* w = reinterpret_cast<const float*>(window);
        constexpr size_t vec = TAPS_PER_PHASE / 4 * 4;

        float32x4_t acc_r4 = vdupq_n_f32(0.0f);
        float32x4_t acc_i4 = vdupq_n_f32(0.0f);
        for (size_t j = 0; j < vec; j += 4) {
            float32x4x2_t s = vld2q_f32(w + 2 * j);
            float32x4_t t4 = vld1q_f32(tap + j);
            acc_r4 = vmlaq_f32(acc_r4, s.val[0], t4);
            acc_i4 = vmlaq_f32(acc_i4, s.val[1], t4);
        }

        float32x2_t r2 = vadd_f32(vget_low_f32(acc_r4), vget_high_f32(acc_r4));
        float32x2_t i2 = vadd_f32(vget_low_f32(acc_i4), vget_high_f32(acc_i4));
        float acc_r = vget_lane_f32(vpadd_f32(r2, r2), 0);
        float acc_i = vget_lane_f32(vpadd_f32(i2, i2), 0);

        for (size_t j = vec; j < TAPS_PER_PHASE; ++j) {
            acc_r += window[j].real() * tap[j];
            acc_i += window[j].imag() * tap[j];
        }
        return std::complex<float>(acc_r, acc_i);
    }

#else

    inline std::complex<float> filter(const std::complex<float>* window, size_t phase) const
    {
        const float* tap = _taps.data() + phase * TAPS_PER_PHASE;
        float acc_r = 0.0f;
        float acc_i = 0.0f;
        for (size_t j = 0; j < TAPS_PER_PHASE; ++j) {
            acc_r += window[j].real() * tap[j];
            acc_i += window[j].imag() * tap[j];
        }
        return std::complex<float>(acc_r, acc_i);
    }

#endif

    std::array<float, INTERP * TAPS_PER_PHASE> _taps{};
    std::array<std::complex<float>, 2 * history> _carry{};
    size_t _phase = 0;
    size_t _input_until_next_output = 1;
};

template <size_t INTERP, size_t DECIM, size_t TAPS_PER_PHASE>
struct RationalResamplerBlock : public cler::BlockBase {
    using Sample = std::complex<float>;
    cler::Channel<Sample> in;

    RationalResamplerBlock(const char* name, const float attenuation,
        const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(Sample) : buffer_size),
          _resampler(attenuation)
    {
        if (buffer_size > 0 && buffer_size * sizeof(Sample) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (attenuation < 0.0f) {
            cler::panic("Attenuation must be non-negative.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Sample>* out)
    {
        auto [read_ptr, read_size]   = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();

        if (read_size == 0)  return cler::Error::NotEnoughSamples;
        if (write_size < 2)  return cler::Error::NotEnoughSpace;

        const size_t inputs_that_fit = ((write_size - 1) * DECIM) / INTERP;
        const size_t num_input = std::min(read_size, inputs_that_fit);
        if (num_input == 0) return cler::Error::NotEnoughSpaceOrSamples;

        const size_t produced = _resampler.process(read_ptr, num_input, write_ptr);
        in.commit_read(num_input);
        out->commit_write(produced);
        return cler::Empty{};
    }

private:
    RationalResampler<INTERP, DECIM, TAPS_PER_PHASE> _resampler;
};
