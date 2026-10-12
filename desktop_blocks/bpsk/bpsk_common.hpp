#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

// Shared pieces of the liquid-free BPSK link (framer, modulator, receiver, deframer).
//
// Frame on air, one symbol per bit, bit 0 -> +1, bit 1 -> -1, bytes MSB first:
//   Barker-13 x2 preamble | length (1 byte, 1..64) | payload | CRC-16-CCITT | zero guard
// The CRC covers length and payload. Packets cross the graph boundary as
// length-prefixed records on a uint8_t channel: [length][payload bytes].
namespace bpsk {

constexpr size_t MAX_PAYLOAD = 64;
constexpr size_t PREAMBLE_SYMBOLS = 26;
constexpr size_t GUARD_SYMBOLS = 8;
constexpr size_t MAX_FRAME_SYMBOLS = PREAMBLE_SYMBOLS + 8 * (1 + MAX_PAYLOAD + 2) + GUARD_SYMBOLS;

constexpr std::array<float, PREAMBLE_SYMBOLS> PREAMBLE = {
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1,
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1,
};

// CRC-16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final xor).
inline uint16_t crc16_ccitt(const uint8_t* data, size_t n, uint16_t crc = 0xFFFF) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// Root-raised-cosine taps, span_symbols * sps + 1 long, normalised to unit energy:
// unit-energy symbols then give Es = 1 per symbol, the convention
// awgn_stddev_for_esn0_db() is written against, and TX RRC * RX RRC peaks at 1.
inline std::vector<float> rrc_taps(unsigned int sps, unsigned int span_symbols, float beta) {
    const size_t n = static_cast<size_t>(span_symbols) * sps + 1;
    std::vector<float> h(n);
    const double b = beta;
    for (size_t i = 0; i < n; ++i) {
        const double t = (static_cast<double>(i) - static_cast<double>(n - 1) / 2.0) / sps;
        double v;
        if (std::fabs(t) < 1e-9) {
            v = 1.0 - b + 4.0 * b / M_PI;
        } else if (b > 0.0 && std::fabs(std::fabs(t) - 1.0 / (4.0 * b)) < 1e-9) {
            v = b / std::sqrt(2.0) * ((1.0 + 2.0 / M_PI) * std::sin(M_PI / (4.0 * b)) +
                                      (1.0 - 2.0 / M_PI) * std::cos(M_PI / (4.0 * b)));
        } else {
            v = (std::sin(M_PI * t * (1.0 - b)) + 4.0 * b * t * std::cos(M_PI * t * (1.0 + b))) /
                (M_PI * t * (1.0 - 16.0 * b * b * t * t));
        }
        h[i] = static_cast<float>(v);
    }
    double e = 0.0;
    for (float v : h) e += static_cast<double>(v) * v;
    const float g = static_cast<float>(1.0 / std::sqrt(e));
    for (float& v : h) v *= g;
    return h;
}

// Per-component AWGN stddev for a target Es/N0 against the unit-energy pulse:
// same formula as awgn_stddev_for_esn0_db() in linear_modem/modulator.hpp, here
// without the liquid dependency. For BPSK Eb = Es.
inline float noise_stddev_for_esn0_db(float esn0_db) {
    return std::sqrt(0.5f * std::pow(10.0f, -esn0_db / 10.0f));
}

// Second-order PI loop with gains from Rice / MATLAB (Digital Communications, App. C):
//   theta = BnTs / (zeta + 1/(4 zeta)),  d = 1 + 2 zeta theta + theta^2,
//   K1 = 4 zeta theta / (d Kp K0),       K2 = 4 theta^2 / (d Kp K0).
// BnTs is per loop update; Kp is the detector slope at zero error; K0 the NCO gain.
struct PiGains {
    float k1;
    float k2;
};

inline PiGains pi_gains(float bn_ts, float zeta, float kp, float k0) {
    const double theta = bn_ts / (zeta + 1.0 / (4.0 * zeta));
    const double d = 1.0 + 2.0 * zeta * theta + theta * theta;
    return {static_cast<float>(4.0 * zeta * theta / (d * kp * k0)),
            static_cast<float>(4.0 * theta * theta / (d * kp * k0))};
}

// In-place iterative radix-2 FFT; size fixed at construction.
class Fft {
public:
    explicit Fft(size_t n) : _n(n), _tw(n / 2) {
        if (n < 2 || (n & (n - 1)) != 0) cler::panic("Fft size must be a power of two");
        for (size_t k = 0; k < n / 2; ++k) {
            _tw[k] = std::polar(1.0f, static_cast<float>(-2.0 * M_PI * static_cast<double>(k) / static_cast<double>(n)));
        }
    }

    size_t size() const { return _n; }

    void forward(std::complex<float>* x) const {
        for (size_t i = 1, j = 0; i < _n; ++i) {
            size_t bit = _n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[i], x[j]);
        }
        for (size_t len = 2; len <= _n; len <<= 1) {
            const size_t step = _n / len;
            for (size_t i = 0; i < _n; i += len) {
                for (size_t k = 0; k < len / 2; ++k) {
                    const std::complex<float> u = x[i + k];
                    const std::complex<float> v = x[i + k + len / 2] * _tw[k * step];
                    x[i + k] = u + v;
                    x[i + k + len / 2] = u - v;
                }
            }
        }
    }

private:
    size_t _n;
    std::vector<std::complex<float>> _tw;
};

// Coarse carrier-frequency estimate from one block: squaring a BPSK signal removes
// the +-1 modulation and leaves a line at twice the offset, so the offset is half
// the peak of |FFT(x^2)|, refined by a parabola through the peak bin and its
// neighbours (Hann window). Unambiguous for |offset| < fs/4.
class CoarseCfoEstimator {
public:
    explicit CoarseCfoEstimator(size_t n) : _fft(n), _buf(n), _win(n) {
        for (size_t i = 0; i < n; ++i) {
            _win[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n)));
        }
    }

    size_t size() const { return _fft.size(); }

    // Peak bin power over mean bin power of the last estimate: a noise-only block
    // stays near ln(N) (about 8 for N = 4096), a BPSK block is orders above.
    double peak_to_mean() const { return _peak_to_mean; }

    // Offset in cycles/sample from size() samples of x.
    double estimate(const std::complex<float>* x) {
        const size_t n = _fft.size();
        for (size_t i = 0; i < n; ++i) _buf[i] = x[i] * x[i] * _win[i];
        _fft.forward(_buf.data());
        size_t k = 0;
        float best = -1.0f;
        double total = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const float p = std::norm(_buf[i]);
            total += p;
            if (p > best) { best = p; k = i; }
        }
        _peak_to_mean = total > 0.0 ? best / (total / static_cast<double>(n)) : 0.0;
        const float pm = std::norm(_buf[(k + n - 1) % n]);
        const float pp = std::norm(_buf[(k + 1) % n]);
        // Parabola on log power: exact for a Gaussian-like peak, close for Hann.
        const double lm = std::log(pm + 1e-30), l0 = std::log(best + 1e-30), lp = std::log(pp + 1e-30);
        const double den = lm - 2.0 * l0 + lp;
        const double delta = den != 0.0 ? 0.5 * (lm - lp) / den : 0.0;
        double bin = static_cast<double>(k) + delta;
        if (bin >= static_cast<double>(n) / 2.0) bin -= static_cast<double>(n);
        return 0.5 * bin / static_cast<double>(n);
    }

private:
    Fft _fft;
    std::vector<std::complex<float>> _buf;
    std::vector<float> _win;
    double _peak_to_mean = 0.0;
};

}  // namespace bpsk
