#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <utility>
#include <vector>

// Counts bit errors against a known symbol sequence that repeats with period
// reference.size(). The receiver delay is unknown and decision-directed carrier
// recovery locks to any constellation symmetry, so the block first searches
// (delay, constellation rotation) once by brute force over one period, then
// counts errors while tracking its position in the reference.
//
// Rotation candidates are the M rotations by 2*pi*k/M; the ones that are not a
// bijection of the constellation onto itself are dropped, which leaves exactly
// the ambiguities the carrier loop can actually settle on (180 deg for BPSK,
// 90 deg multiples for QPSK/QAM, 45 deg multiples for 8-PSK).
struct BERCounterBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    BERCounterBlock(const char* name,
                    modulation_scheme scheme,
                    std::vector<uint8_t> reference,
                    size_t skip_symbols = 2000,
                    size_t search_symbols = 512,
                    size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _ref(std::move(reference)),
          _skip_symbols(skip_symbols), _search_symbols(search_symbols) {
        if (_ref.empty()) {
            cler::panic("BERCounterBlock requires a non-empty reference sequence");
        }
        modemcf mod = modemcf_create(scheme);
        if (!mod) {
            cler::panic("BERCounterBlock: unsupported modulation scheme");
        }
        _bps = modemcf_get_bps(mod);
        const unsigned int M = 1u << _bps;
        std::vector<uint8_t> perm(M);
        std::vector<bool> seen(M);
        for (unsigned int k = 0; k < M; ++k) {
            const std::complex<float> rot = std::polar(1.0f, 2.0f * static_cast<float>(M_PI) * k / M);
            std::fill(seen.begin(), seen.end(), false);
            bool bijection = true;
            for (unsigned int s = 0; s < M; ++s) {
                std::complex<float> x;
                modemcf_modulate(mod, s, &x);
                unsigned int d = 0;
                modemcf_demodulate(mod, x * rot, &d);
                if (seen[d]) { bijection = false; break; }
                seen[d] = true;
                perm[s] = static_cast<uint8_t>(d);
            }
            if (bijection) _perms.push_back(perm);
        }
        modemcf_destroy(mod);
        _window.reserve(_search_symbols);
        _scratch.resize(4096);
    }

    // Thread-safe readouts for the GUI thread.
    bool aligned() const { return _aligned.load(std::memory_order_relaxed); }
    uint64_t bits() const { return _bits.load(std::memory_order_relaxed); }
    uint64_t bit_errors() const { return _errors.load(std::memory_order_relaxed); }
    double ber() const {
        const uint64_t b = bits();
        return b ? static_cast<double>(bit_errors()) / static_cast<double>(b) : 0.0;
    }

    // Callable from the GUI thread; takes effect at the top of the next procedure().
    void reset() { _reset_request.store(true, std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (_reset_request.exchange(false, std::memory_order_relaxed)) {
            _window.clear();
            _skipped = 0;
            _aligned.store(false, std::memory_order_relaxed);
            _bits.store(0, std::memory_order_relaxed);
            _errors.store(0, std::memory_order_relaxed);
        }

        const size_t n = std::min(in.size(), _scratch.size());
        if (n == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.readN(_scratch.data(), n);

        for (size_t i = 0; i < n; ++i) {
            const uint8_t rx = _scratch[i];
            if (_skipped < _skip_symbols) {
                ++_skipped;
                continue;
            }
            if (!_aligned.load(std::memory_order_relaxed)) {
                _window.push_back(rx);
                if (_window.size() == _search_symbols) align();
                continue;
            }
            count(rx);
        }
        return cler::Empty{};
    }

private:
    void count(uint8_t rx) {
        const uint8_t expect = _perms[_perm_index][_ref[_pos]];
        const unsigned int diff = static_cast<unsigned int>(rx ^ expect) & ((1u << _bps) - 1u);
        _errors.fetch_add(static_cast<uint64_t>(__builtin_popcount(diff)), std::memory_order_relaxed);
        _bits.fetch_add(_bps, std::memory_order_relaxed);
        if (++_pos == _ref.size()) _pos = 0;
    }

    void align() {
        const size_t P = _ref.size();
        const size_t W = _window.size();
        size_t best_errors = W + 1, best_delay = 0, best_perm = 0;
        for (size_t p = 0; p < _perms.size(); ++p) {
            const uint8_t* perm = _perms[p].data();
            for (size_t d = 0; d < P; ++d) {
                size_t errors = 0;
                for (size_t i = 0; i < W && errors < best_errors; ++i) {
                    if (_window[i] != perm[_ref[(d + i) % P]]) ++errors;
                }
                if (errors < best_errors) {
                    best_errors = errors;
                    best_delay = d;
                    best_perm = p;
                }
            }
        }
        // A wrong hypothesis leaves ~(1 - 1/M) of the window in error; only a
        // clearly better match is an alignment.
        if (best_errors * 5 < W) {
            _perm_index = best_perm;
            _pos = (best_delay + W) % P;
            _aligned.store(true, std::memory_order_relaxed);
        }
        _window.clear();
    }

    std::vector<uint8_t> _ref;
    std::vector<std::vector<uint8_t>> _perms;
    std::vector<uint8_t> _window;
    std::vector<uint8_t> _scratch;
    unsigned int _bps = 1;
    size_t _skip_symbols;
    size_t _search_symbols;
    size_t _skipped = 0;
    size_t _pos = 0;
    size_t _perm_index = 0;

    std::atomic<bool> _aligned{false};
    std::atomic<bool> _reset_request{false};
    std::atomic<uint64_t> _bits{0};
    std::atomic<uint64_t> _errors{0};
};
