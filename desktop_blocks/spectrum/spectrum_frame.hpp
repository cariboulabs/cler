#pragma once

#include <cstddef>
#include <cstdint>

// One averaged power spectrum, dB quantised to u8 on a fixed scale so a
// consumer can draw an axis without autoscale: dB = db_min + bins[i] * db_step.
// Fixed-size so it is a POD channel element; n <= MAX_N bins are valid.
struct SpectrumFrame {
    static constexpr size_t MAX_N = 4096;
    uint32_t gen;
    double center_hz;
    double rate_hz;
    uint16_t n;
    float db_min, db_step;
    uint8_t bins[MAX_N];
};
