#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/ais/ais.hpp"
#include "liquid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

// One AIS channel: complex baseband at `sample_rate` (an integer multiple of
// 9600 baud, default 48 kS/s) -> channel lowpass -> GMSK quadrature demod ->
// GMSK receive filter -> burst decoder -> parsed messages.
//
// Bursts are short (256 bits) and arrive after noise, so instead of a timing
// PLL the decoder correlates the discriminator output with the 0101 training
// sequence (passed through the same receive chain at construction). The best
// window gives the symbol phase (widest eye) and the DC offset (carrier
// error, the quartile midpoint of the eye samples); the burst is then sampled
// at a fixed rate, NRZI-decoded and handed to the HDLC deframer. An IQ power
// squelch against a tracked noise floor keeps noise from triggering.
struct AISDecoderBlock : public cler::BlockBase {
    static constexpr double BAUD = 9600.0;
    cler::Channel<std::complex<float>> in;

    AISDecoderBlock(const char* name, double sample_rate = 48e3, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double sps = sample_rate / BAUD;
        if (std::fabs(sps - std::round(sps)) > 1e-6 || sps < 4.0 || sps > 16.0) {
            cler::panic("AISDecoderBlock: sample_rate must be 4..16 x 9600");
        }
        _sps = static_cast<unsigned int>(std::lround(sps));
        _win = PREAMBLE_SYMBOLS * _sps;
        _ring.assign(_win, 0.0f);
        _tmpl.assign(4 * _sps, 0.0f);

        // channel lowpass +/-8 kHz (AIS channels are 25 kHz apart; a tighter
        // edge distorts the discriminator for offset carriers)
        liquid_firdes_kaiser(CH_TAPS, static_cast<float>(8e3 / sample_rate), 60.0f, 0.0f, _ch_taps.data());
        float g = 0.0f;
        for (float t : _ch_taps) g += t;
        for (float& t : _ch_taps) t /= g;
        _chf = firfilt_crcf_create(_ch_taps.data(), CH_TAPS);
        // +/-2400 Hz deviation (h = 0.5) -> +/-1
        _demod = freqdem_create(static_cast<float>(BAUD / 4.0 / sample_rate));
        _rxf = firfilt_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, _sps, 3, 0.4f, 0);
        if (!_chf || !_demod || !_rxf) cler::panic("AISDecoderBlock: liquid create failed");
        _pw_alpha = 1.0f / (8.0f * _sps);
        build_template();
    }

    ~AISDecoderBlock() {
        if (_chf) firfilt_crcf_destroy(_chf);
        if (_demod) freqdem_destroy(_demod);
        if (_rxf) firfilt_rrrf_destroy(_rxf);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<ais::Message>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;
        // a frame needs >= 200 symbols, so this many samples cannot outrun the output
        const size_t n = std::min(rsize, wsize * 200 * _sps);
        size_t written = 0;

        for (size_t i = 0; i < n; ++i) {
            liquid_float_complex x;
            firfilt_crcf_push(_chf, rptr[i]);
            firfilt_crcf_execute(_chf, &x);
            float f;
            freqdem_demodulate(_demod, x, &f);
            f = std::clamp(f, -3.0f, 3.0f);
            firfilt_rrrf_push(_rxf, f);
            float y;
            firfilt_rrrf_execute(_rxf, &y);
            if (sample(y, std::norm(rptr[i])) && written < wsize) {
                ais::Message m;
                if (ais::parse(_framer.payload(), _framer.length(), m)) {
                    wptr[written++] = m;
                    _messages.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(n);
        out->commit_write(written);
        return cler::Empty{};
    }

    uint32_t frames_ok() const { return _framer.frames_ok(); }
    uint32_t frames_bad_crc() const { return _framer.frames_bad_crc(); }
    uint64_t messages() const { return _messages.load(std::memory_order_relaxed); }
    uint64_t bursts() const { return _bursts.load(std::memory_order_relaxed); }

private:
    static constexpr unsigned int CH_TAPS = 63;
    static constexpr unsigned int PREAMBLE_SYMBOLS = 16;   // of the 24 sent; leaves a plateau to pick from
    static constexpr float CORR_THRESHOLD = 0.6f;

    void build_template() {
        gmskmod mod = gmskmod_create(_sps, 3, 0.4f);
        freqdem dem = freqdem_create(static_cast<float>(BAUD / 4.0 / (BAUD * _sps)));
        firfilt_crcf chf = firfilt_crcf_create(_ch_taps.data(), CH_TAPS);
        firfilt_rrrf rxf = firfilt_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, _sps, 3, 0.4f, 0);
        std::vector<float> out;
        std::vector<liquid_float_complex> buf(_sps);
        // NRZI levels of the 0101 training: 1 1 0 0 repeating
        for (int i = 0; i < 64; ++i) {
            gmskmod_modulate(mod, ((i / 2) % 2 == 0) ? 1u : 0u, buf.data());
            for (unsigned int k = 0; k < _sps; ++k) {
                liquid_float_complex x;
                firfilt_crcf_push(chf, buf[k]);
                firfilt_crcf_execute(chf, &x);
                float f, y;
                freqdem_demodulate(dem, x, &f);
                firfilt_rrrf_push(rxf, f);
                firfilt_rrrf_execute(rxf, &y);
                out.push_back(y);
            }
        }
        std::copy_n(out.begin() + 40 * _sps, 4 * _sps, _tmpl.begin());
        float m = 0.0f;
        for (float v : _tmpl) m = std::max(m, std::fabs(v));
        for (float& v : _tmpl) v /= m;
        gmskmod_destroy(mod);
        freqdem_destroy(dem);
        firfilt_crcf_destroy(chf);
        firfilt_rrrf_destroy(rxf);
    }

    // one receive-filtered discriminator sample; returns true when a CRC-valid
    // frame completed
    bool sample(float y, float power) {
        _ring[_w] = y;
        _w = (_w + 1) % _win;
        ++_cnt;
        _pw += _pw_alpha * (power - _pw);
        if (_cnt < 4 * _win) _nf = _pw;
        else if (_pw < _nf) _nf = _pw;
        else _nf *= 1.0005f;

        if (!_decoding) {
            if (_cnt < 4 * _win) return false;
            float c = 0.0f, a = 0.0f;
            const size_t P = _tmpl.size();
            for (size_t k = 0; k < _win; ++k) {
                const float v = _ring[(_w + k) % _win];
                c += v * _tmpl[k % P];
                a += std::fabs(v);
            }
            const float nc = std::fabs(c) / (a + 1e-6f);
            const bool loud = _pw > 4.0f * _nf;
            if (loud && nc > CORR_THRESHOLD && nc >= _peak - 0.03f) {
                if (nc > _peak) _peak = nc;
                _peak_age = 0;
                pick_phase_and_dc();
                _peak_cnt = _cnt;
            } else if (_peak > CORR_THRESHOLD && (nc < _peak - 0.08f || ++_peak_age > 2 * _sps)) {
                // plateau ended: decode the kept window (the preamble), then continue live
                _decoding = true;
                _syms = 0;
                _prev = false;
                _peak = 0.0f;
                _peak_age = 0;
                _bursts.fetch_add(1, std::memory_order_relaxed);
                _sample_mod = static_cast<unsigned int>(((_peak_cnt - static_cast<long>(_win) + _ph) % _sps + _sps) % _sps);
                bool done = false;
                for (size_t j = 0; j < _win; ++j) {
                    const long c0 = _cnt - static_cast<long>(_win) + static_cast<long>(j);
                    if (static_cast<unsigned int>(((c0 % _sps) + _sps) % _sps) != _sample_mod) continue;
                    if (decide(_ring[(_w + j) % _win])) done = true;
                }
                return done;
            }
            return false;
        }

        // _cnt was incremented for this sample, so its index is _cnt - 1: the
        // same convention the replay loop above uses (c0 = _cnt - _win + j)
        if (static_cast<unsigned int>((_cnt - 1) % _sps) != _sample_mod) return false;
        const bool done = decide(y);
        const uint32_t frames = _framer.frames_ok() + _framer.frames_bad_crc();
        if (frames != _frames_seen || _syms > 1100 || (_syms > 40 && !_framer.in_frame())) {
            _frames_seen = frames;
            _decoding = false;
        }
        return done;
    }

    bool decide(float v) {
        const bool level = (v - _dc) > 0.0f;
        const bool bit = !(level ^ _prev);   // NRZI: no transition = 1
        _prev = level;
        ++_syms;
        return _framer.push_bit(bit);
    }

    // sampling phase with the widest eye over the window; DC from the quartile
    // midpoint of those eye samples (robust to the burst's onset transient)
    void pick_phase_and_dc() {
        float best = -1.0f;
        std::array<float, PREAMBLE_SYMBOLS> e{};
        for (unsigned int ph = 0; ph < _sps; ++ph) {
            size_t m = 0;
            for (size_t j = ph; j < _win; j += _sps) e[m++] = _ring[(_w + j) % _win];
            std::sort(e.begin(), e.begin() + m);
            const float q1 = e[m / 4], q3 = e[3 * m / 4];
            if (q3 - q1 > best) {
                best = q3 - q1;
                _ph = ph;
                _dc = 0.5f * (q1 + q3);
            }
        }
    }

    unsigned int _sps = 5;
    size_t _win = 80;
    std::array<float, CH_TAPS> _ch_taps{};
    firfilt_crcf _chf = nullptr;
    freqdem _demod = nullptr;
    firfilt_rrrf _rxf = nullptr;
    std::vector<float> _tmpl, _ring;
    size_t _w = 0;
    long _cnt = 0;
    float _pw = 0.0f, _nf = 1e9f, _pw_alpha = 0.025f;
    float _peak = 0.0f;
    unsigned int _peak_age = 0;
    long _peak_cnt = 0;
    unsigned int _ph = 0, _sample_mod = 0;
    float _dc = 0.0f;
    bool _decoding = false, _prev = false;
    unsigned int _syms = 0;
    uint32_t _frames_seen = 0;
    ais::Deframer _framer;
    std::atomic<uint64_t> _messages{0}, _bursts{0};
};
