#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Linear modulator: symbol indices 0..M-1 in, RRC-shaped complex baseband out at
// `sps` samples per symbol. The pulse taps are normalised to unit energy, so the
// mean output sample power is Es/sps with Es = 1 (liquid normalises its
// constellations to unit average symbol energy). That fixes the SNR convention
// the AWGN level and the EVM estimate are quoted against.
inline unsigned int scheme_bits_per_symbol(modulation_scheme scheme) {
    modemcf m = modemcf_create(scheme);
    if (!m) cler::panic("unsupported modulation scheme");
    const unsigned int bps = modemcf_get_bps(m);
    modemcf_destroy(m);
    return bps;
}

// Per-component stddev of complex AWGN for a target Es/N0, given the unit-energy
// pulse normalisation above: the complex noise variance is 10^(-esn0/10), which
// after the unit-energy matched filter is exactly N0 against Es = 1.
inline float awgn_stddev_for_esn0_db(float esn0_db) {
    return std::sqrt(0.5f * std::pow(10.0f, -esn0_db / 10.0f));
}

struct LinearModulatorBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    LinearModulatorBlock(const char* name,
                   modulation_scheme scheme,
                   unsigned int sps,
                   float beta,
                   unsigned int filter_delay_symbols = 5,
                   size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _sps(sps) {
        if (sps < 2) {
            cler::panic("LinearModulatorBlock requires samples/symbol >= 2");
        }
        _mod = modemcf_create(scheme);
        if (!_mod) {
            cler::panic("LinearModulatorBlock: unsupported modulation scheme");
        }

        const unsigned int h_len = 2 * sps * filter_delay_symbols + 1;
        std::vector<float> h(h_len);
        liquid_firdes_prototype(LIQUID_FIRFILT_RRC, sps, filter_delay_symbols, beta, 0.0f, h.data());
        float energy = 0.0f;
        for (float t : h) energy += t * t;
        const float g = 1.0f / std::sqrt(energy);
        for (float& t : h) t *= g;
        _interp = firinterp_crcf_create(sps, h.data(), h_len);

        _sym_scratch.resize(1024);
        _samp_scratch.resize(_sym_scratch.size() * sps);
    }

    ~LinearModulatorBlock() {
        firinterp_crcf_destroy(_interp);
        modemcf_destroy(_mod);
    }

    unsigned int bits_per_symbol() const { return modemcf_get_bps(_mod); }
    unsigned int samples_per_symbol() const { return _sps; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        const size_t n = std::min({in.size(), out->space() / _sps, _sym_scratch.size()});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_sym_scratch.data(), n);
        for (size_t i = 0; i < n; ++i) {
            std::complex<float> sym;
            modemcf_modulate(_mod, _sym_scratch[i], &sym);
            firinterp_crcf_execute(_interp, sym, _samp_scratch.data() + i * _sps);
        }
        out->writeN(_samp_scratch.data(), n * _sps);
        return cler::Empty{};
    }

private:
    unsigned int _sps;
    modemcf _mod = nullptr;
    firinterp_crcf _interp = nullptr;
    std::vector<uint8_t> _sym_scratch;
    std::vector<std::complex<float>> _samp_scratch;
};
