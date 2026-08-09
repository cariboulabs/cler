#pragma once

#include "liquid.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>

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

    std::array<float, INTERP * TAPS_PER_PHASE> _taps{};
    std::array<std::complex<float>, 2 * history> _carry{};
    size_t _phase = 0;
    size_t _input_until_next_output = 1;
};
