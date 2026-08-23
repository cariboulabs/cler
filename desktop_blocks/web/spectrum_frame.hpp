#pragma once

#include <cstdint>

struct SpectrumFrame {
    uint32_t gen;
    double center_hz;
    double rate_hz;
    uint16_t n;
    float db_min, db_step;
    uint8_t bins[4096];
};
