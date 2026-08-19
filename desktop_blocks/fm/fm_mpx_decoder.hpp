#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/fm/rds.hpp"
#include "liquid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>

// Broadcast FM multiplex decoder. Input is the demodulated MPX baseband
// (float, normalised so +/-1 = +/-deviation) at mpx_rate >= 120 kHz; output is
// interleaved L,R audio at mpx_rate / audio_decim. One PLL on the 19 kHz pilot
// drives both the 38 kHz stereo subcarrier and the 57 kHz RDS carrier.
struct FMMpxDecoderBlock : public cler::BlockBase {
    static constexpr size_t MAX_DECIM = 16;
    cler::Channel<float> in;

    FMMpxDecoderBlock(const char* name,
                      double mpx_rate,
                      size_t audio_decim = 5,
                      double deemphasis_us = 50.0,
                      size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _fs(mpx_rate),
          _decim(audio_decim)
    {
        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (mpx_rate < 120e3) cler::panic("FMMpxDecoderBlock: mpx_rate must be >= 120 kHz");
        if (audio_decim == 0 || audio_decim > MAX_DECIM) cler::panic("FMMpxDecoderBlock: bad audio_decim");
        _audio_fs = mpx_rate / static_cast<double>(audio_decim);
        if (_audio_fs < 32e3) cler::panic("FMMpxDecoderBlock: audio rate must be >= 32 kHz");

        // pilot PLL: 2nd order, ~15 Hz loop bandwidth
        const double wn = 2.0 * M_PI * 15.0 / mpx_rate;
        _kp = static_cast<float>(2.0 * 0.707 * wn);
        _ki = static_cast<float>(wn * wn);
        _omega = static_cast<float>(2.0 * M_PI * 19e3 / mpx_rate);
        _pd_alpha = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * 200.0 / mpx_rate));
        _stat_alpha = static_cast<float>(1.0 / (0.1 * mpx_rate));

        // 15 kHz audio lowpass folded into the decimator
        const float fc = static_cast<float>(15e3 / mpx_rate);
        const float df = static_cast<float>(4e3 / mpx_rate);
        unsigned int h_len = estimate_req_filter_len(df, 60.0f);
        if (h_len % 2 == 0) ++h_len;
        if (h_len > MAX_TAPS) h_len = MAX_TAPS;
        liquid_firdes_kaiser(h_len, fc, 60.0f, 0.0f, _audio_taps.data());
        float dc = 0.0f;
        for (unsigned int i = 0; i < h_len; ++i) dc += _audio_taps[i];
        for (unsigned int i = 0; i < h_len; ++i) _audio_taps[i] /= dc;
        _lpr_decim = firdecim_rrrf_create(static_cast<unsigned int>(audio_decim), _audio_taps.data(), h_len);
        _lmr_decim = firdecim_rrrf_create(static_cast<unsigned int>(audio_decim), _audio_taps.data(), h_len);
        if (!_lpr_decim || !_lmr_decim) cler::panic("FMMpxDecoderBlock: firdecim create failed");
        set_deemphasis_us(deemphasis_us);

        // RDS: 57 kHz -> baseband, integer-decimate to ~24 kHz with a real
        // anti-alias filter (57 kHz aliases to DC at 19 kHz), resample to
        // 8 samples per half-bit (19 kHz), then a polyphase symbol
        // synchroniser with an RRC matched filter.
        _rds_decim = static_cast<unsigned int>(std::max(1.0, std::floor(mpx_rate / 24e3)));
        if (_rds_decim > MAX_DECIM) cler::panic("FMMpxDecoderBlock: mpx_rate too high for RDS path");
        const float rfc = static_cast<float>(4e3 / mpx_rate);
        const float rdf = static_cast<float>((mpx_rate / _rds_decim / 2.0 - 4e3) / mpx_rate);
        unsigned int rlen = estimate_req_filter_len(rdf, 60.0f);
        if (rlen % 2 == 0) ++rlen;
        if (rlen > MAX_TAPS) rlen = MAX_TAPS;
        liquid_firdes_kaiser(rlen, rfc, 60.0f, 0.0f, _rds_taps.data());
        _rds_firdecim = firdecim_crcf_create(_rds_decim, _rds_taps.data(), rlen);
        const float r = static_cast<float>(RDS_FS * _rds_decim / mpx_rate);
        _rds_resamp = resamp_crcf_create(r, 7, std::min(0.45f, 0.45f * r), 60.0f, 64);
        _rds_sync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, RDS_SPS, 4, 0.9f, 32);
        if (!_rds_firdecim || !_rds_resamp || !_rds_sync) cler::panic("FMMpxDecoderBlock: RDS create failed");
        symsync_crcf_set_lf_bw(_rds_sync, 0.02f);
        symsync_crcf_set_output_rate(_rds_sync, 1);
    }

    ~FMMpxDecoderBlock() {
        if (_lpr_decim) firdecim_rrrf_destroy(_lpr_decim);
        if (_lmr_decim) firdecim_rrrf_destroy(_lmr_decim);
        if (_rds_firdecim) firdecim_crcf_destroy(_rds_firdecim);
        if (_rds_resamp) resamp_crcf_destroy(_rds_resamp);
        if (_rds_sync) symsync_crcf_destroy(_rds_sync);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        size_t frames = std::min(rsize / _decim, wsize / 2);
        if (frames == 0) return cler::Error::NotEnoughSpaceOrSamples;

        const bool stereo_on = _stereo_enabled.load(std::memory_order_relaxed);
        const float de = _deemph_alpha.load(std::memory_order_relaxed);
        std::array<float, MAX_DECIM> lpr{}, lmr{};

        for (size_t f = 0; f < frames; ++f) {
            const float* x = rptr + f * _decim;
            for (size_t i = 0; i < _decim; ++i) {
                const float s = x[i];
                const float sn = std::sin(_theta), cs = std::cos(_theta);

                // phase detector on the pilot
                _pd_i += _pd_alpha * (s * cs - _pd_i);
                _pd_q += _pd_alpha * (-s * sn - _pd_q);
                const float err = std::atan2(_pd_q, _pd_i);
                _freq += _ki * err;
                _theta += _omega + _freq + _kp * err;
                if (_theta > 2.0f * static_cast<float>(M_PI)) _theta -= 2.0f * static_cast<float>(M_PI);
                else if (_theta < 0.0f) _theta += 2.0f * static_cast<float>(M_PI);

                _pil_i += _stat_alpha * (_pd_i - _pil_i);
                _pil_q2 += _stat_alpha * (_pd_q * _pd_q - _pil_q2);

                lpr[i] = s;
                // pilot locks as cos(theta) = sin(phi); subcarrier is sin(2 phi) = -sin(2 theta)
                lmr[i] = -2.0f * s * (2.0f * sn * cs);

                rds_sample(s, sn, cs);
            }
            float sum = 0.0f, diff = 0.0f;
            firdecim_rrrf_execute(_lpr_decim, lpr.data(), &sum);
            firdecim_rrrf_execute(_lmr_decim, lmr.data(), &diff);
            if (!stereo_on || !_locked) diff = 0.0f;
            const float l = 0.5f * (sum + diff), r = 0.5f * (sum - diff);
            _de_l += de * (l - _de_l);
            _de_r += de * (r - _de_r);
            wptr[2 * f] = _de_l;
            wptr[2 * f + 1] = _de_r;
        }

        const float snr = _pil_i * _pil_i / (_pil_q2 + 1e-12f);
        const float snr_db = 10.0f * std::log10(snr + 1e-12f);
        _locked = (_pil_i > 0.02f) && (snr_db > 8.0f);
        _pilot_snr_db.store(snr_db, std::memory_order_relaxed);
        _pilot_level.store(2.0f * _pil_i, std::memory_order_relaxed);
        _stereo_locked.store(_locked, std::memory_order_relaxed);

        in.commit_read(frames * _decim);
        out->commit_write(frames * 2);
        return cler::Empty{};
    }

    float pilot_snr_db() const { return _pilot_snr_db.load(std::memory_order_relaxed); }
    // pilot amplitude relative to full deviation (nominal 0.08-0.10)
    float pilot_level() const { return _pilot_level.load(std::memory_order_relaxed); }
    bool stereo_locked() const { return _stereo_locked.load(std::memory_order_relaxed); }
    void set_stereo(bool on) { _stereo_enabled.store(on, std::memory_order_relaxed); }
    bool stereo() const { return _stereo_enabled.load(std::memory_order_relaxed); }
    void set_deemphasis_us(double us) {
        const float a = us <= 0.0 ? 1.0f
            : static_cast<float>(1.0 - std::exp(-1.0 / (us * 1e-6 * _audio_fs)));
        _deemph_alpha.store(a, std::memory_order_relaxed);
    }
    double audio_rate() const { return _audio_fs; }

    uint64_t rds_halfbits() const { return _rds_halfbits.load(std::memory_order_relaxed); }

    rds::Station rds_station() const {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        return _rds_snapshot;
    }
    void rds_reset() {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        _rds_reset_request = true;
        _rds_snapshot = rds::Station{};
    }

private:
    static constexpr size_t MAX_TAPS = 512;
    static constexpr double RDS_FS = 19000.0;   // 8 samples per half-bit at 2375 baud
    static constexpr unsigned int RDS_SPS = 8;

    void rds_sample(float s, float sn, float cs) {
        // e^{-j3 theta}
        const float c3 = cs * (4.0f * cs * cs - 3.0f);
        const float s3 = sn * (3.0f - 4.0f * sn * sn);
        _rds_mix[_rds_fill++] = {s * c3, -s * s3};
        if (_rds_fill < _rds_decim) return;
        _rds_fill = 0;
        liquid_float_complex z;
        firdecim_crcf_execute(_rds_firdecim, _rds_mix.data(), &z);
        liquid_float_complex rs[4];
        unsigned int n = 0;
        resamp_crcf_execute(_rds_resamp, z, rs, &n);
        for (unsigned int k = 0; k < n; ++k) {
            liquid_float_complex sym[2];
            unsigned int m = 0;
            symsync_crcf_execute(_rds_sync, &rs[k], 1, sym, &m);
            for (unsigned int j = 0; j < m; ++j) rds_halfbit(sym[j]);
        }
    }

    void rds_halfbit(std::complex<float> v) {
        _rds_halfbits.fetch_add(1, std::memory_order_relaxed);
        // BPSK phase: slow average of v^2 gives 2*phase
        const std::complex<float> v2 = v * v;
        _rds_ph += 0.005f * (v2 - _rds_ph);
        const float ph = 0.5f * std::atan2(_rds_ph.imag(), _rds_ph.real());
        const float y = v.real() * std::cos(ph) + v.imag() * std::sin(ph);

        // biphase: each bit is two opposite half-bits; track which pairing is right
        const float d = y - _rds_prev;
        _rds_pair[_rds_parity] += 0.01f * (std::fabs(d) - _rds_pair[_rds_parity]);
        const int best = _rds_pair[0] >= _rds_pair[1] ? 0 : 1;
        if (_rds_parity == best) {
            const bool bit = d > 0.0f;
            const bool data = bit ^ _rds_prev_bit;
            _rds_prev_bit = bit;
            if (_rds.push_bit(data)) publish_rds();
        }
        _rds_prev = y;
        _rds_parity ^= 1;
    }

    void publish_rds() {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        if (_rds_reset_request) {
            _rds.reset();
            _rds_reset_request = false;
        }
        _rds_snapshot = _rds.station();
    }

    double _fs, _audio_fs;
    size_t _decim;
    float _kp, _ki, _omega, _pd_alpha, _stat_alpha;
    float _theta = 0.0f, _freq = 0.0f;
    float _pd_i = 0.0f, _pd_q = 0.0f;
    float _pil_i = 0.0f, _pil_q2 = 1.0f;
    bool _locked = false;
    float _de_l = 0.0f, _de_r = 0.0f;

    std::array<float, MAX_TAPS> _audio_taps{};
    firdecim_rrrf _lpr_decim = nullptr, _lmr_decim = nullptr;

    unsigned int _rds_decim = 1;
    std::array<float, MAX_TAPS> _rds_taps{};
    std::array<liquid_float_complex, MAX_DECIM> _rds_mix{};
    unsigned int _rds_fill = 0;
    firdecim_crcf _rds_firdecim = nullptr;
    resamp_crcf _rds_resamp = nullptr;
    symsync_crcf _rds_sync = nullptr;
    std::complex<float> _rds_ph{0.0f, 0.0f};
    float _rds_prev = 0.0f;
    float _rds_pair[2] = {0.0f, 0.0f};
    int _rds_parity = 0;
    bool _rds_prev_bit = false;
    rds::Decoder _rds;
    mutable std::mutex _rds_mutex;
    rds::Station _rds_snapshot;
    bool _rds_reset_request = false;

    std::atomic<uint64_t> _rds_halfbits{0};
    std::atomic<float> _pilot_snr_db{-99.0f};
    std::atomic<float> _pilot_level{0.0f};
    std::atomic<bool> _stereo_locked{false};
    std::atomic<bool> _stereo_enabled{true};
    std::atomic<float> _deemph_alpha{1.0f};
};
