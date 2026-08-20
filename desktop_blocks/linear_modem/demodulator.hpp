#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Linear demodulator: complex baseband in, hard symbol decisions out on the
// first output and the recovered (carrier- and timing-corrected, unit-energy)
// constellation points on the second, one point per symbol.
//
// Chain: AGC -> RRC matched filter with symbol timing recovery (symsync) ->
// symbol-rate power normalisation -> decision-directed carrier recovery (the
// modem's phase error drives an NCO PLL).
//
// The power normalisation is what makes EVM meaningful: liquid's constellations
// have unit average symbol energy, so scaling the recovered symbols to unit mean
// power puts them on the same scale as the ideal points.
struct LinearDemodulatorBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    LinearDemodulatorBlock(const char* name,
                     modulation_scheme scheme,
                     unsigned int sps,
                     float beta,
                     unsigned int filter_delay_symbols = 5,
                     float pll_bandwidth = 0.002f,
                     float lock_evm = 0.5f,
                     size_t buffer_size = 8192)
        : cler::BlockBase(name), in(buffer_size), _lock_evm(lock_evm) {
        if (sps < 2) {
            cler::panic("LinearDemodulatorBlock requires samples/symbol >= 2");
        }
        _mod = modemcf_create(scheme);
        if (!_mod) {
            cler::panic("LinearDemodulatorBlock: unsupported modulation scheme");
        }
        _agc = agc_crcf_create();
        agc_crcf_set_bandwidth(_agc, 1e-3f);
        _sync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, sps, filter_delay_symbols, beta, 32);
        symsync_crcf_set_output_rate(_sync, 1);
        symsync_crcf_set_lf_bw(_sync, 0.02f);
        _nco = nco_crcf_create(LIQUID_VCO);
        nco_crcf_pll_set_bandwidth(_nco, pll_bandwidth);

        _scratch = 4096;
        _in_buf.resize(_scratch);
        _agc_buf.resize(_scratch);
        _sym_buf.resize(_scratch);
        _out_syms.resize(_scratch);
        _out_pts.resize(_scratch);
        _last_rate_time = std::chrono::steady_clock::now();
    }

    ~LinearDemodulatorBlock() {
        nco_crcf_destroy(_nco);
        symsync_crcf_destroy(_sync);
        agc_crcf_destroy(_agc);
        modemcf_destroy(_mod);
    }

    unsigned int bits_per_symbol() const { return modemcf_get_bps(_mod); }

    // Thread-safe: readable from the GUI thread while procedure() runs.
    float evm_percent() const { return 100.0f * std::sqrt(_err_acc.load(std::memory_order_relaxed)); }
    float snr_db() const {
        const float e = _err_acc.load(std::memory_order_relaxed);
        return e > 0.0f ? -10.0f * std::log10(e) : 99.0f;
    }
    bool locked() const { return _locked.load(std::memory_order_relaxed); }
    float symbol_rate() const { return _sym_rate.load(std::memory_order_relaxed); }
    float carrier_offset() const { return _freq.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out_symbols,
                                                     cler::ChannelBase<std::complex<float>>* out_constellation) {
        // symsync consumes every sample handed to it, so the input is bounded by
        // the smaller output space: it can never emit more symbols than samples in.
        const size_t n = std::min({in.size(), out_symbols->space(), out_constellation->space(), _scratch});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_in_buf.data(), n);
        agc_crcf_execute_block(_agc, _in_buf.data(), static_cast<unsigned int>(n), _agc_buf.data());

        unsigned int ny = 0;
        symsync_crcf_execute(_sync, _agc_buf.data(), static_cast<unsigned int>(n), _sym_buf.data(), &ny);

        constexpr float alpha = 0.01f;
        for (unsigned int i = 0; i < ny; ++i) {
            std::complex<float> v;
            nco_crcf_mix_down(_nco, _sym_buf[i], &v);

            _pwr = (1.0f - alpha) * _pwr + alpha * std::norm(v);
            if (_pwr < 1e-12f) _pwr = 1e-12f;
            v /= std::sqrt(_pwr);

            unsigned int sym = 0;
            modemcf_demodulate(_mod, v, &sym);
            std::complex<float> ideal;
            modemcf_get_demodulator_sample(_mod, &ideal);
            _err = (1.0f - alpha) * _err + alpha * std::norm(v - ideal);

            nco_crcf_pll_step(_nco, modemcf_get_demodulator_phase_error(_mod));
            nco_crcf_step(_nco);

            _out_syms[i] = static_cast<uint8_t>(sym);
            _out_pts[i] = v;
        }

        if (ny > 0) {
            out_symbols->writeN(_out_syms.data(), ny);
            out_constellation->writeN(_out_pts.data(), ny);
            _err_acc.store(_err, std::memory_order_relaxed);
            _locked.store(std::sqrt(_err) < _lock_evm, std::memory_order_relaxed);
            _freq.store(nco_crcf_get_frequency(_nco), std::memory_order_relaxed);
            update_rate(ny);
        }
        return cler::Empty{};
    }

private:
    void update_rate(unsigned int ny) {
        _sym_count += ny;
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - _last_rate_time).count();
        if (dt >= 0.5f) {
            _sym_rate.store(_sym_count / dt, std::memory_order_relaxed);
            _sym_count = 0;
            _last_rate_time = now;
        }
    }

    modemcf _mod = nullptr;
    agc_crcf _agc = nullptr;
    symsync_crcf _sync = nullptr;
    nco_crcf _nco = nullptr;

    size_t _scratch = 0;
    std::vector<std::complex<float>> _in_buf, _agc_buf, _sym_buf, _out_pts;
    std::vector<uint8_t> _out_syms;

    float _pwr = 1.0f;
    float _err = 1.0f;
    float _lock_evm;
    size_t _sym_count = 0;
    std::chrono::steady_clock::time_point _last_rate_time;

    std::atomic<float> _err_acc{1.0f};
    std::atomic<float> _sym_rate{0.0f};
    std::atomic<float> _freq{0.0f};
    std::atomic<bool> _locked{false};
};
