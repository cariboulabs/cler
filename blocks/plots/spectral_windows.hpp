#pragma once
#include <cmath>

enum class SpectralWindow {
    BlackmanHarris,
    Hamming,
    Hann,
    Rectangular,
    Kaiser,
    FlatTop,
};

// Simple constexpr approximation of I₀(x)
inline constexpr float bessel_i0(float x) {
    // Abramowitz & Stegun approximation
    float sum = 1.0f;
    float y = x * x / 4.0f;
    float t = y;
    int k = 1;
    while (t > 1e-8f) {
        sum += t;
        ++k;
        t *= y / (k * k);
    }
    return sum;
}

// Kaiser window needs beta parameter
inline constexpr float kaiser_window(float x, float beta) {
    float t = 2.0f * x - 1.0f; // scale x to [-1, 1]
    return bessel_i0(beta * std::sqrt(1.0f - t * t)) / bessel_i0(beta);
}

// Flat Top window: flat in freq, poor res
inline constexpr float flattop_window(float x) {
    return 1.0f
        - 1.93f * std::cos(2 * cler::PI * x)
        + 1.29f * std::cos(4 * cler::PI * x)
        - 0.388f * std::cos(6 * cler::PI * x)
        + 0.0322f * std::cos(8 * cler::PI * x);
}

inline constexpr float spectral_window_function(SpectralWindow type, float x, float beta = 8.6f) {
    switch (type) {
        case SpectralWindow::BlackmanHarris:
            return 0.35875f - 0.48829f * std::cos(2 * cler::PI * x)
                           + 0.14128f * std::cos(4 * cler::PI * x)
                           - 0.01168f * std::cos(6 * cler::PI * x);
        case SpectralWindow::Hamming:
            return 0.54f - 0.46f * std::cos(2 * cler::PI * x);
        case SpectralWindow::Hann:
            return 0.5f * (1.0f - std::cos(2 * cler::PI * x));
        case SpectralWindow::Rectangular:
            return 1.0f;
        case SpectralWindow::Kaiser:
            return kaiser_window(x, beta);
        case SpectralWindow::FlatTop:
            return flattop_window(x);
    }
    return 0.0f;
}
