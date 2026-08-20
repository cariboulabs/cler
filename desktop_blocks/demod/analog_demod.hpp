#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"

#include <atomic>
#include <cmath>
#include <algorithm>
#include <complex>

// Runtime-switchable analog demodulator: complex channel at `channel_rate`
// (WBFM's rate; must be audio_decim x 48 kHz) -> mono audio at 48 kHz.
//   WBFM : quadrature demod at channel rate, audio lowpass + decimate,
//          50 us de-emphasis
//   NBFM : decimate to audio rate first, quadrature demod (2.5 kHz deviation)
//   AM   : decimate, magnitude, DC block
//   USB/LSB: decimate, then a +/-1.6 kHz frequency-translated 1.6 kHz
//          lowpass (= 0..3.2 kHz for USB, -3.2..0 for LSB), real part
// Mode switches are applied between procedure() calls; each switch resets the
// per-mode state and mutes 20 ms of audio, so the filter fill and the AM
// carrier estimate settle into silence instead of a full-scale thump.
struct AnalogDemodBlock : public cler::BlockBase {
    enum class Mode { WBFM, NBFM, AM, USB, LSB };
    cler::Channel<std::complex<float>> in;

    AnalogDemodBlock(const char* name, double channel_rate, Mode mode = Mode::WBFM,
                     size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _mode(mode), _requested(mode)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double d = channel_rate / AUDIO_RATE;
        if (std::fabs(d - std::round(d)) > 1e-6 || d < 1.0 || d > MAX_DECIM) {
            cler::panic("AnalogDemodBlock: channel_rate must be 1..16 x 48 kHz");
        }
        _decim = static_cast<unsigned int>(std::lround(d));

        // Anti-alias lowpass shared by the WBFM audio decimator and the complex
        // predecimator: flat to 15 kHz (WBFM mono audio), >=60 dB by 24 kHz so
        // nothing folds into the 48 kHz output. liquid centres the transition
        // band on fc, so both edges are rate-derived; a fixed transition width
        // aliases as soon as channel_rate exceeds ~360 kHz.
        float taps[MAX_TAPS];
        const float df = static_cast<float>(9e3 / channel_rate);
        unsigned int h_len = estimate_req_filter_len(df, 60.0f);
        if (h_len % 2 == 0) ++h_len;
        if (h_len > MAX_TAPS) cler::panic("AnalogDemodBlock: anti-alias filter too long");
        liquid_firdes_kaiser(h_len, static_cast<float>(19.5e3 / channel_rate), 60.0f, 0.0f, taps);
        float dc = 0.0f;
        for (unsigned int i = 0; i < h_len; ++i) dc += taps[i];
        for (unsigned int i = 0; i < h_len; ++i) taps[i] /= dc;
        _audio_decim = firdecim_rrrf_create(_decim, taps, h_len);
        _iq_decim = firdecim_crcf_create(_decim, taps, h_len);
        _wbfm = freqdem_create(static_cast<float>(75e3 / channel_rate));
        _nbfm = freqdem_create(static_cast<float>(2.5e3 / AUDIO_RATE));
        _ssb_bpf = firfilt_crcf_create_kaiser(129, static_cast<float>(1.6e3 / AUDIO_RATE), 60.0f, 0.0f);
        _ssb_nco = nco_crcf_create(LIQUID_NCO);
        if (!_audio_decim || !_iq_decim || !_wbfm || !_nbfm || !_ssb_bpf || !_ssb_nco) {
            cler::panic("AnalogDemodBlock: liquid create failed");
        }
        _deemph_alpha = static_cast<float>(1.0 - std::exp(-1.0 / (50e-6 * AUDIO_RATE)));
        apply_mode(mode);
    }

    ~AnalogDemodBlock() {
        if (_audio_decim) firdecim_rrrf_destroy(_audio_decim);
        if (_iq_decim) firdecim_crcf_destroy(_iq_decim);
        if (_wbfm) freqdem_destroy(_wbfm);
        if (_nbfm) freqdem_destroy(_nbfm);
        if (_ssb_bpf) firfilt_crcf_destroy(_ssb_bpf);
        if (_ssb_nco) nco_crcf_destroy(_ssb_nco);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        const Mode want = _requested.load(std::memory_order_relaxed);
        if (want != _mode) apply_mode(want);

        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        const size_t frames = std::min(rsize / _decim, wsize);
        if (frames == 0) return cler::Error::NotEnoughSpaceOrSamples;

        switch (_mode) {
            case Mode::WBFM:
                for (size_t f = 0; f < frames; ++f) {
                    freqdem_demodulate_block(_wbfm,
                        const_cast<liquid_float_complex*>(rptr + f * _decim),
                        _decim, _mpx);
                    float a;
                    firdecim_rrrf_execute(_audio_decim, _mpx, &a);
                    _de += _deemph_alpha * (a - _de);
                    wptr[f] = _de;
                }
                break;
            case Mode::NBFM:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    float a;
                    freqdem_demodulate(_nbfm, z, &a);
                    _de += _deemph_alpha * (a - _de);
                    wptr[f] = _de;
                }
                break;
            case Mode::AM:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    const float mag = std::abs(z);
                    // while muted the carrier estimate is a running mean, so
                    // the 42 ms tracker is handed the carrier level (not one
                    // modulation peak) and starts settled instead of thumping
                    if (f < _settle) _am_dc += (mag - _am_dc) / static_cast<float>(++_am_n);
                    else _am_dc += 0.0005f * (mag - _am_dc);
                    wptr[f] = 4.0f * (mag - _am_dc);
                }
                break;
            case Mode::USB:
            case Mode::LSB:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    // mix down, filter, mix up with one NCO phase per sample: the
                    // e^{-j.theta.n}/e^{+j.theta.n} cancel and the taps become
                    // h[k]e^{j.theta.k}, i.e. a lowpass translated to +/-1.6 kHz
                    nco_crcf_mix_down(_ssb_nco, z, &z);
                    firfilt_crcf_push(_ssb_bpf, z);
                    firfilt_crcf_execute(_ssb_bpf, &z);
                    nco_crcf_mix_up(_ssb_nco, z, &z);
                    nco_crcf_step(_ssb_nco);
                    wptr[f] = 2.0f * z.real();
                }
                break;
        }
        if (_settle) {
            const size_t n = std::min(_settle, frames);
            std::fill(wptr, wptr + n, 0.0f);
            _settle -= n;
        }
        in.commit_read(frames * _decim);
        out->commit_write(frames);
        return cler::Empty{};
    }

    void set_mode(Mode m) { _requested.store(m, std::memory_order_relaxed); }
    Mode mode() const { return _requested.load(std::memory_order_relaxed); }
    static const char* mode_name(Mode m) {
        switch (m) {
            case Mode::WBFM: return "WBFM";
            case Mode::NBFM: return "NBFM";
            case Mode::AM: return "AM";
            case Mode::USB: return "USB";
            default: return "LSB";
        }
    }
    double audio_rate() const { return AUDIO_RATE; }

private:
    static constexpr double AUDIO_RATE = 48e3;
    static constexpr size_t MAX_DECIM = 16;
    static constexpr unsigned int MAX_TAPS = 512;

    void apply_mode(Mode m) {
        _mode = m;
        freqdem_reset(_wbfm);
        freqdem_reset(_nbfm);
        firdecim_rrrf_reset(_audio_decim);
        firdecim_crcf_reset(_iq_decim);
        firfilt_crcf_reset(_ssb_bpf);
        nco_crcf_reset(_ssb_nco);
        nco_crcf_set_frequency(_ssb_nco,
            static_cast<float>(2.0 * M_PI * 1.6e3 / AUDIO_RATE) * (m == Mode::LSB ? -1.0f : 1.0f));
        _de = 0.0f;
        _am_dc = 0.0f;
        _am_n = 0;
        _settle = static_cast<size_t>(AUDIO_RATE * 0.02);
    }

    unsigned int _decim = 5;
    Mode _mode;
    std::atomic<Mode> _requested;
    float _mpx[MAX_DECIM];
    firdecim_rrrf _audio_decim = nullptr;
    firdecim_crcf _iq_decim = nullptr;
    freqdem _wbfm = nullptr, _nbfm = nullptr;
    firfilt_crcf _ssb_bpf = nullptr;
    nco_crcf _ssb_nco = nullptr;
    float _deemph_alpha = 0.0f, _de = 0.0f, _am_dc = 0.0f;
    size_t _settle = 0, _am_n = 0;
};
