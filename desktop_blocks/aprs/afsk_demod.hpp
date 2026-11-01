#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/aprs/aprs.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Bell 202 / AFSK1200 receiver: discriminator audio (or a soundcard's line in)
// at `sample_rate` -> two one-baud tone correlators -> clock recovery -> NRZI
// -> the AX.25 deframer -> parsed APRS packets.
//
// The tone decision is the normalised difference of the mark (1200 Hz) and
// space (2200 Hz) correlator magnitudes, so it is independent of level: half
// amplitude or a mismatched FM demodulator gain changes both magnitudes
// together. The one-baud window is 1200 Hz wide, which swallows the few Hz of
// audio shift a real link adds.
//
// Clock recovery is direwolf's simplest slicer: a phase accumulator stepping
// one symbol per baud that samples on wrap, nudged toward the eye centre on
// every transition. APRS bursts open with many HDLC flags, so it locks well
// before the data.
struct AFSKDemodBlock : public cler::BlockBase {
    static constexpr double BAUD = 1200.0, MARK_HZ = 1200.0, SPACE_HZ = 2200.0;

    cler::Channel<float> in;

    AFSKDemodBlock(const char* name, double sample_rate = 48e3, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size)
    {
        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double sps = sample_rate / BAUD;
        if (std::fabs(sps - std::round(sps)) > 1e-6 || sps < 8.0 || sps > 128.0) {
            cler::panic("AFSKDemodBlock: sample_rate must be 8..128 x 1200");
        }
        _n = static_cast<size_t>(std::lround(sps));
        _ring.assign(_n, 0.0f);
        _mark_c.resize(_n); _mark_s.resize(_n); _space_c.resize(_n); _space_s.resize(_n);
        for (size_t k = 0; k < _n; ++k) {
            const double tm = 2.0 * M_PI * MARK_HZ * k / sample_rate;
            const double ts = 2.0 * M_PI * SPACE_HZ * k / sample_rate;
            _mark_c[k] = static_cast<float>(std::cos(tm));
            _mark_s[k] = static_cast<float>(std::sin(tm));
            _space_c[k] = static_cast<float>(std::cos(ts));
            _space_s[k] = static_cast<float>(std::sin(ts));
        }
        _pll_step = static_cast<float>(2.0 * BAUD / sample_rate);
        _lp_alpha = 4.0f / static_cast<float>(_n);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<aprs::Packet>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;
        // the shortest APRS frame is ~160 bits, so this many samples cannot
        // produce more packets than the output has room for
        const size_t n = std::min(rsize, wsize * 150 * _n);
        size_t written = 0;

        for (size_t i = 0; i < n; ++i) {
            if (sample(rptr[i]) && written < wsize) {
                aprs::Packet p;
                if (aprs::parse(_framer.payload(), _framer.length(), p)) {
                    wptr[written++] = p;
                    _packets.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(n);
        out->commit_write(written);
        return cler::Empty{};
    }

    uint32_t frames_ok() const { return _framer.frames_ok(); }
    uint32_t frames_bad_crc() const { return _framer.frames_bad_crc(); }
    uint64_t packets() const { return _packets.load(std::memory_order_relaxed); }

private:
    // one audio sample; returns true when a CRC-valid frame completed
    bool sample(float x) {
        _ring[_w] = x;
        _w = _w + 1 == _n ? 0 : _w + 1;

        float mc = 0.0f, ms = 0.0f, sc = 0.0f, ss = 0.0f;
        for (size_t k = 0; k < _n; ++k) {
            const float v = _ring[(_w + k) % _n];
            mc += v * _mark_c[k];
            ms += v * _mark_s[k];
            sc += v * _space_c[k];
            ss += v * _space_s[k];
        }
        const float m = std::sqrt(mc * mc + ms * ms), s = std::sqrt(sc * sc + ss * ss);
        const float d = (m - s) / (m + s + 1e-9f);
        _lp += _lp_alpha * (d - _lp);
        const bool level = _lp > 0.0f;

        bool done = false;
        const float prev_pll = _pll;
        _pll += _pll_step;
        if (_pll >= 1.0f) {
            _pll -= 2.0f;
            if (prev_pll > 0.0f) done = decide(level);
        }
        if (level != _prev_level) {
            _pll *= PLL_INERTIA;   // pull the sampling instant back to the eye centre
            _prev_level = level;
        }
        return done;
    }

    bool decide(bool level) {
        const bool bit = !(level ^ _prev_nrzi);   // NRZI: no transition = 1
        _prev_nrzi = level;
        return _framer.push_bit(bit);
    }

    static constexpr float PLL_INERTIA = 0.75f;

    size_t _n = 40, _w = 0;
    std::vector<float> _ring, _mark_c, _mark_s, _space_c, _space_s;
    float _pll = 0.0f, _pll_step = 0.05f, _lp = 0.0f, _lp_alpha = 0.1f;
    bool _prev_level = false, _prev_nrzi = false;
    aprs::Deframer _framer;
    std::atomic<uint64_t> _packets{0};
};
