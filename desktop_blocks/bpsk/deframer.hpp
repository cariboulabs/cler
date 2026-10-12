#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/bpsk/bpsk_common.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

// Soft BPSK symbols in (1 per symbol), packets out as length-prefixed records
// ([length][payload], inverse of BPSKFramerBlock). Only CRC-checked frames leave.
//
// Frame sync: the 26-symbol preamble is correlated against the last 26 symbols,
// normalised to [0, 1] by both energies. After the metric first reaches
// `threshold`, the search runs PEAK_SEARCH more symbols and takes the maximum:
// a window 13 symbols early lines the second Barker-13 up with the first and can
// pass the threshold on its own when data or idle symbols precede the preamble.
// The symbols after the true peak are replayed from a short history as the
// first length bits. The correlation phase de-rotates the frame, so a
// carrier loop that settled on 180 degrees (negative peak) yields the same bits.
struct BPSKDeframerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    BPSKDeframerBlock(const char* name, float threshold = 0.7f, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _threshold(threshold) {
        if (buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (!(threshold > 0.0f && threshold < 1.0f)) {
            cler::panic("BPSKDeframerBlock threshold must be in (0, 1)");
        }
    }

    // Thread-safe counters. A preamble that fails the length check counts as bad_length.
    uint64_t frames_ok() const { return _ok.load(std::memory_order_relaxed); }
    uint64_t crc_failures() const { return _crc_fail.load(std::memory_order_relaxed); }
    uint64_t bad_lengths() const { return _bad_len.load(std::memory_order_relaxed); }
    uint64_t preambles_detected() const { return _detected.load(std::memory_order_relaxed); }
    uint64_t inverted_frames() const { return _inverted.load(std::memory_order_relaxed); }
    float last_peak() const { return _last_peak.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        bool progress = false;
        if (_pending > 0) {
            if (out->space() < _pending) return cler::Error::NotEnoughSpace;
            out->writeN(_record.data(), _pending);
            _pending = 0;
            progress = true;
        }

        auto [rptr, rsize] = in.read_dbf();
        size_t i = 0;
        // Stop after a completed frame so its record leaves before the next one.
        while (i < rsize && _pending == 0) step(rptr[i++]);
        if (i > 0) {
            in.commit_read(i);
            progress = true;
        }
        if (_pending > 0 && out->space() >= _pending) {
            out->writeN(_record.data(), _pending);
            _pending = 0;
        }
        return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                        : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSamples);
    }

private:
    enum class State { Search, Length, Payload, Crc };

    void step(std::complex<float> z) {
        if (_state != State::Search) {
            take_bit(std::real(z * _derot) < 0.0f ? 1u : 0u);
            return;
        }
        _hist[_n % HISTORY] = z;
        ++_n;
        _win[_wpos] = z;
        _wpos = (_wpos + 1) % bpsk::PREAMBLE_SYMBOLS;
        if (_filled < bpsk::PREAMBLE_SYMBOLS) {
            ++_filled;
            if (_filled < bpsk::PREAMBLE_SYMBOLS) return;
        }
        std::complex<float> corr{0.0f, 0.0f};
        float energy = 0.0f;
        for (size_t k = 0; k < bpsk::PREAMBLE_SYMBOLS; ++k) {
            const std::complex<float> w = _win[(_wpos + k) % bpsk::PREAMBLE_SYMBOLS];
            corr += bpsk::PREAMBLE[k] * w;
            energy += std::norm(w);
        }
        const float e = std::max(energy, 1e-12f);
        const float metric = std::abs(corr) / std::sqrt(static_cast<float>(bpsk::PREAMBLE_SYMBOLS) * e);
        if (_hold > 0) {
            if (metric > _best) {
                _best = metric;
                _best_corr = corr;
                _best_n = _n;
            }
            if (--_hold > 0) return;
            start_frame();
            // Symbols after the peak window already went by: replay them.
            for (uint64_t k = _best_n; k < _n && _state != State::Search; ++k) {
                take_bit(std::real(_hist[k % HISTORY] * _derot) < 0.0f ? 1u : 0u);
            }
            return;
        }
        if (metric >= _threshold) {
            _hold = PEAK_SEARCH;
            _best = metric;
            _best_corr = corr;
            _best_n = _n;
        }
    }

    void start_frame() {
        _hold = 0;
        _detected.fetch_add(1, std::memory_order_relaxed);
        _last_peak.store(_best, std::memory_order_relaxed);
        _derot = std::conj(_best_corr) / std::abs(_best_corr);
        if (std::real(_best_corr) < 0.0f) _inverted.fetch_add(1, std::memory_order_relaxed);
        _state = State::Length;
        _bits = 0;
        _byte = 0;
        _nbytes = 0;
    }

    void take_bit(unsigned int bit) {
        _byte = static_cast<uint8_t>((_byte << 1) | bit);
        if (++_bits < 8) return;
        _bits = 0;
        const uint8_t b = _byte;
        _byte = 0;
        switch (_state) {
        case State::Length:
            if (b == 0 || b > bpsk::MAX_PAYLOAD) {
                _bad_len.fetch_add(1, std::memory_order_relaxed);
                reset_search();
                return;
            }
            _record[0] = b;
            _nbytes = 1;
            _state = State::Payload;
            return;
        case State::Payload:
            _record[_nbytes++] = b;
            if (_nbytes == 1u + _record[0]) _state = State::Crc;
            return;
        case State::Crc:
            _crc_bytes[_crc_count++] = b;
            if (_crc_count < 2) return;
            {
                const uint16_t rx = static_cast<uint16_t>((_crc_bytes[0] << 8) | _crc_bytes[1]);
                if (rx == bpsk::crc16_ccitt(_record.data(), _nbytes)) {
                    _pending = _nbytes;
                    _ok.fetch_add(1, std::memory_order_relaxed);
                } else {
                    _crc_fail.fetch_add(1, std::memory_order_relaxed);
                }
            }
            reset_search();
            return;
        case State::Search:
            return;
        }
    }

    void reset_search() {
        _state = State::Search;
        _crc_count = 0;
        _filled = 0;
        _win.fill({0.0f, 0.0f});
        _wpos = 0;
    }

    static constexpr size_t PEAK_SEARCH = 16;
    static constexpr size_t HISTORY = 32;

    float _threshold;
    State _state = State::Search;
    std::array<std::complex<float>, bpsk::PREAMBLE_SYMBOLS> _win{};
    size_t _wpos = 0;
    size_t _filled = 0;
    size_t _hold = 0;
    std::array<std::complex<float>, HISTORY> _hist{};
    uint64_t _n = 0;
    uint64_t _best_n = 0;
    float _best = 0.0f;
    std::complex<float> _best_corr{0.0f, 0.0f};
    std::complex<float> _derot{1.0f, 0.0f};

    unsigned int _bits = 0;
    uint8_t _byte = 0;
    size_t _nbytes = 0;
    std::array<uint8_t, 2> _crc_bytes{};
    size_t _crc_count = 0;
    std::array<uint8_t, 1 + bpsk::MAX_PAYLOAD> _record{};
    size_t _pending = 0;

    std::atomic<uint64_t> _ok{0};
    std::atomic<uint64_t> _crc_fail{0};
    std::atomic<uint64_t> _bad_len{0};
    std::atomic<uint64_t> _detected{0};
    std::atomic<uint64_t> _inverted{0};
    std::atomic<float> _last_peak{0.0f};
};
