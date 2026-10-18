#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/bpsk/bpsk_common.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>
#include <vector>

// Liquid-free BPSK receiver: complex baseband in at `sps`, soft symbols out
// (1 per symbol, carrier-corrected, unit amplitude). For a TX and RX that share
// no clock: the carrier offset and the sample-clock rate are both estimated.
//
// Chain, run per block of `block_size` input samples (latency: one block):
//  1. Coarse CFO: |FFT(x^2)| peak / 2 on the block itself, smoothed across
//     blocks, removed by an NCO. Range |CFO| < fs/4.
//  2. RRC matched filter (TX taps), decimated to 2 samples/symbol.
//  3. AGC on the matched-filter output to symbol amplitude 1 (mean power
//     1 - beta/4 for random data at any timing phase).
//  4. Timing: Gardner TED e = Re{(y[k] - y[k-1]) conj(y[k-1/2])} on complex
//     samples (rotation-invariant, so it runs before carrier recovery),
//     piecewise-parabolic Farrow (alpha 0.5), PI loop, period clamped.
//  5. Carrier: Costas loop at 1 sps, e = sgn(I) Q, PI loop, frequency clamped.
// Loop gains: bpsk::pi_gains (Rice). Detector slopes are the S-curve slopes at
// zero error, measured by averaging each detector over 20k noiseless random
// symbols through TX RRC * RX RRC at unit amplitude, beta 0.35, at a swept
// offset: Gardner Kp = 1.356 per symbol of timing error (e > 0 = sampling late),
// Costas Kp = 1.0 per radian.
// Both loops update once per symbol with NCO gain K0 = 1.
struct BPSKReceiverConfig {
    unsigned int sps = 8;
    float beta = 0.35f;
    unsigned int span_symbols = 10;
    size_t block_size = 4096;         // coarse-CFO FFT size, power of two
    float cfo_smoothing = 0.25f;      // weight of each new block's estimate
    float agc_bandwidth = 1e-3f;      // per 2-sps sample
    float timing_bn_ts = 0.01f;
    float timing_zeta = 1.0f;
    float timing_kp = 1.356f;
    float max_rate_deviation = 0.02f; // symbol period clamp, +-fraction
    float carrier_bn_ts = 0.01f;
    float carrier_zeta = 0.707f;
    float carrier_kp = 1.0f;
    float max_carrier_freq = 0.05f;   // Costas frequency clamp, cycles/symbol
    float lock_threshold = 0.7f;      // smoothed cos(2 phi)
};

struct BPSKReceiverBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    BPSKReceiverBlock(const char* name, double sample_rate_hz, BPSKReceiverConfig cfg = {},
        size_t buffer_size = 16384)
        : cler::BlockBase(name), in(buffer_size), _cfg(cfg), _fs(sample_rate_hz),
          _decim(cfg.sps / 2), _cfo(cfg.block_size) {
        if (cfg.sps < 2 || cfg.sps % 2 != 0) cler::panic("BPSKReceiverBlock requires an even sps >= 2");
        if (cfg.block_size % _decim != 0) cler::panic("BPSKReceiverBlock block_size must be a multiple of sps/2");
        if (buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        _h = bpsk::rrc_taps(cfg.sps, cfg.span_symbols, cfg.beta);
        _block.resize(cfg.block_size);
        _line.assign(_h.size() - 1 + cfg.block_size, {0.0f, 0.0f});
        _s2.assign(3 + cfg.block_size / _decim, {0.0f, 0.0f});
        const size_t max_symbols = static_cast<size_t>(
            static_cast<double>(cfg.block_size / _decim) / 2.0 / (1.0 - cfg.max_rate_deviation)) + 4;
        _staging.resize(max_symbols);
        _pre.resize(max_symbols);
        _snap_pre.resize(SNAPSHOT_POINTS);
        _snap_post.resize(SNAPSHOT_POINTS);
        _agc_target = 1.0f - cfg.beta / 4.0f;
        _tg = bpsk::pi_gains(cfg.timing_bn_ts, cfg.timing_zeta, cfg.timing_kp, 1.0f);
        _cg = bpsk::pi_gains(cfg.carrier_bn_ts, cfg.carrier_zeta, cfg.carrier_kp, 1.0f);
    }

    // Thread-safe readouts.
    double cfo_hz() const { return _cfo_hz.load(std::memory_order_relaxed); }
    float costas_freq_hz() const { return _costas_hz.load(std::memory_order_relaxed); }
    float timing_error() const { return _ted.load(std::memory_order_relaxed); }
    float mu() const { return _mu.load(std::memory_order_relaxed); }
    // Positive when the TX sample clock runs fast relative to ours. Measured from the
    // samples the interpolator advanced over RATE_WINDOW symbols (exact up to the
    // timing jitter at the window ends), not from the loop integrator, which carries
    // the loop noise; smoothed across windows. 0 until the first window completes.
    float rate_error_ppm() const { return _rate_ppm.load(std::memory_order_relaxed); }
    bool locked() const { return _locked.load(std::memory_order_relaxed); }
    float evm_db() const { return _evm_db.load(std::memory_order_relaxed); }
    float lock_metric() const { return _lock_metric.load(std::memory_order_relaxed); }
    // Symbol-centre points of the last processed block before and after the Costas
    // loop (for a constellation display). Copies up to max points; any thread.
    size_t snapshot(std::complex<float>* before_costas, std::complex<float>* after_costas, size_t max) {
        std::lock_guard<std::mutex> lock(_snap_mutex);
        const size_t n = std::min(max, _snap_n);
        std::copy_n(_snap_pre.data(), n, before_costas);
        std::copy_n(_snap_post.data(), n, after_costas);
        return n;
    }

    bpsk::PiGains timing_gains() const { return _tg; }
    bpsk::PiGains carrier_gains() const { return _cg; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        bool progress = drain(out);
        if (_staged > _sent) {
            return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                            : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSpace);
        }
        auto [rptr, rsize] = in.read_dbf();
        if (rsize > 0) {
            const size_t n = std::min(rsize, _block.size() - _fill);
            std::copy_n(rptr, n, _block.data() + _fill);
            in.commit_read(n);
            _fill += n;
            progress = true;
            if (_fill == _block.size()) {
                process_block();
                _fill = 0;
                drain(out);
            }
        }
        return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                        : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSamples);
    }

private:
    bool drain(cler::ChannelBase<std::complex<float>>* out) {
        if (_staged == _sent) return false;
        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(wsize, _staged - _sent);
        if (n > 0) std::copy_n(_staging.data() + _sent, n, wptr);
        out->commit_write(n);
        _sent += n;
        if (_sent == _staged) _staged = _sent = 0;
        return n > 0;
    }

    void process_block() {
        const size_t n = _block.size();
        const size_t taps = _h.size();

        // 1. coarse CFO, estimated on this block and applied to it
        const double f = _cfo.estimate(_block.data());
        if (_cfo.peak_to_mean() > CFO_MIN_PEAK_TO_MEAN) {
            _cfo_est = _cfo_valid ? _cfo_est + _cfg.cfo_smoothing * (f - _cfo_est) : f;
            _cfo_valid = true;
        }
        for (size_t i = 0; i < n; ++i) {
            _line[taps - 1 + i] = _block[i] * std::polar(1.0f, static_cast<float>(-_nco_phase));
            _nco_phase += 2.0 * M_PI * _cfo_est;
            if (_nco_phase > M_PI) _nco_phase -= 2.0 * M_PI;
            if (_nco_phase < -M_PI) _nco_phase += 2.0 * M_PI;
        }

        // 2. matched filter, decimated to 2 sps; 3. AGC
        const size_t m_out = n / _decim;
        if (!_agc_init) {
            double p = 0.0;
            for (size_t m = 0; m < m_out; ++m) p += std::norm(mf_at(m * _decim));
            _agc_power = std::max(p / static_cast<double>(m_out), 1e-20);
            _agc_init = true;
        }
        for (size_t m = 0; m < m_out; ++m) {
            const std::complex<float> y = mf_at(m * _decim);
            _agc_power += _cfg.agc_bandwidth * (std::norm(y) - _agc_power);
            const float g = static_cast<float>(std::sqrt(_agc_target / std::max(_agc_power, 1e-20)));
            _s2[3 + m] = y * g;
        }
        std::copy(_line.end() - static_cast<long>(taps - 1), _line.end(), _line.begin());

        // 4. timing + 5. carrier
        const size_t first = _staged;
        recover(static_cast<long>(m_out));
        publish_snapshot(first);
        std::copy(_s2.begin() + static_cast<long>(m_out), _s2.begin() + static_cast<long>(m_out + 3), _s2.begin());

        _cfo_hz.store(_cfo_est * _fs, std::memory_order_relaxed);
    }

    // Skipped, never waited for, while a reader holds the lock.
    void publish_snapshot(size_t first) {
        std::unique_lock<std::mutex> lock(_snap_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return;
        const size_t n = std::min(_staged - first, SNAPSHOT_POINTS);
        std::copy_n(_pre.data() + first, n, _snap_pre.data());
        std::copy_n(_staging.data() + first, n, _snap_post.data());
        _snap_n = n;
    }

    // Matched-filter output for input index i of the current block.
    std::complex<float> mf_at(size_t i) const {
        const std::complex<float>* x = _line.data() + i + _h.size() - 1;
        std::complex<float> acc{0.0f, 0.0f};
        for (size_t k = 0; k < _h.size(); ++k) acc += _h[k] * *(x - k);
        return acc;
    }

    // Interpolants every half symbol over _s2 (indices -3..-1 are the previous
    // block's last samples), alternating mid-symbol and symbol-centre strobes.
    void recover(long m) {
        const std::complex<float>* x = _s2.data() + 3;
        const double lo = 1.0 - _cfg.max_rate_deviation, hi = 1.0 + _cfg.max_rate_deviation;
        while (true) {
            const double fk = std::floor(_t);
            const long k = static_cast<long>(fk);
            if (k + 2 >= m) break;
            const float mu = static_cast<float>(_t - fk);
            const std::complex<float> y = farrow(x[k - 1], x[k], x[k + 1], x[k + 2], mu);
            if (_mid) {
                _y_mid = y;
            } else {
                const float e = std::real((y - _y_prev) * std::conj(_y_mid));
                _t_int = std::clamp(_t_int + _tg.k2 * e, -_cfg.max_rate_deviation, _cfg.max_rate_deviation);
                _step = std::clamp(1.0 - (_tg.k1 * e + _t_int), lo, hi);
                _y_prev = y;
                rate_window(_t);
                carrier(y);
                _ted.store(e, std::memory_order_relaxed);
                _mu.store(mu, std::memory_order_relaxed);
            }
            _mid = !_mid;
            _t += _step;
        }
        _t -= static_cast<double>(m);
        _t_base += static_cast<double>(m);
    }

    // t: position of this symbol-centre strobe within the current block.
    void rate_window(double t) {
        const double abs_t = _t_base + t;
        if (_rate_count == 0) _rate_start = abs_t;
        if (++_rate_count <= RATE_WINDOW) return;
        const double eps = 2.0 * static_cast<double>(RATE_WINDOW) / (abs_t - _rate_start) - 1.0;
        _rate_est = _rate_valid ? _rate_est + 0.5 * (eps - _rate_est) : eps;
        _rate_valid = true;
        _rate_ppm.store(static_cast<float>(_rate_est * 1e6), std::memory_order_relaxed);
        _rate_count = 1;
        _rate_start = abs_t;
    }

    // Piecewise-parabolic Farrow, alpha = 0.5, between x0 (mu = 0) and x1 (mu = 1).
    static std::complex<float> farrow(std::complex<float> xm1, std::complex<float> x0,
        std::complex<float> x1, std::complex<float> x2, float mu) {
        constexpr float a = 0.5f;
        const std::complex<float> v2 = a * (x2 - x1 - x0 + xm1);
        const std::complex<float> v1 = -a * x2 + (1.0f + a) * x1 - (1.0f - a) * x0 - a * xm1;
        return (v2 * mu + v1) * mu + x0;
    }

    void carrier(std::complex<float> y) {
        const std::complex<float> z = y * std::polar(1.0f, -_phase);
        const float e = (z.real() >= 0.0f ? 1.0f : -1.0f) * z.imag();
        const float fmax = 2.0f * static_cast<float>(M_PI) * _cfg.max_carrier_freq;
        _c_int = std::clamp(_c_int + _cg.k2 * e, -fmax, fmax);
        _phase += _cg.k1 * e + _c_int;
        if (_phase > static_cast<float>(M_PI)) _phase -= 2.0f * static_cast<float>(M_PI);
        if (_phase < -static_cast<float>(M_PI)) _phase += 2.0f * static_cast<float>(M_PI);

        const float p = std::norm(z);
        if (p > 1e-12f) {
            _cos2 += LOCK_ALPHA * ((z.real() * z.real() - z.imag() * z.imag()) / p - _cos2);
        }
        const std::complex<float> d(z.real() >= 0.0f ? 1.0f : -1.0f, 0.0f);
        _evm += LOCK_ALPHA * (std::norm(z - d) - _evm);
        _pre[_staged] = y;
        _staging[_staged++] = z;

        _lock_metric.store(_cos2, std::memory_order_relaxed);
        _locked.store(_cos2 > _cfg.lock_threshold, std::memory_order_relaxed);
        _evm_db.store(10.0f * std::log10(std::max(_evm, 1e-12f)), std::memory_order_relaxed);
        _costas_hz.store(_c_int / (2.0f * static_cast<float>(M_PI)) * static_cast<float>(_fs / _cfg.sps),
            std::memory_order_relaxed);
    }

    static constexpr double CFO_MIN_PEAK_TO_MEAN = 30.0;
    static constexpr size_t RATE_WINDOW = 2048;
    static constexpr size_t SNAPSHOT_POINTS = 512;
    static constexpr float LOCK_ALPHA = 0.01f;

    BPSKReceiverConfig _cfg;
    double _fs;
    size_t _decim;
    bpsk::CoarseCfoEstimator _cfo;
    std::vector<float> _h;
    std::vector<std::complex<float>> _block;
    std::vector<std::complex<float>> _line;
    std::vector<std::complex<float>> _s2;
    std::vector<std::complex<float>> _staging;
    std::vector<std::complex<float>> _pre;
    std::mutex _snap_mutex;
    std::vector<std::complex<float>> _snap_pre;
    std::vector<std::complex<float>> _snap_post;
    size_t _snap_n = 0;
    size_t _fill = 0;
    size_t _staged = 0;
    size_t _sent = 0;

    double _cfo_est = 0.0;
    bool _cfo_valid = false;
    double _nco_phase = 0.0;

    bool _agc_init = false;
    double _agc_power = 1.0;
    float _agc_target;

    bpsk::PiGains _tg;
    bpsk::PiGains _cg;
    double _t = 0.0;
    double _t_base = 0.0;
    double _step = 1.0;
    size_t _rate_count = 0;
    double _rate_start = 0.0;
    double _rate_est = 0.0;
    bool _rate_valid = false;
    float _t_int = 0.0f;
    bool _mid = false;
    std::complex<float> _y_mid{0.0f, 0.0f};
    std::complex<float> _y_prev{0.0f, 0.0f};

    float _phase = 0.0f;
    float _c_int = 0.0f;
    float _cos2 = 0.0f;
    float _evm = 1.0f;

    std::atomic<double> _cfo_hz{0.0};
    std::atomic<float> _costas_hz{0.0f};
    std::atomic<float> _ted{0.0f};
    std::atomic<float> _mu{0.0f};
    std::atomic<float> _rate_ppm{0.0f};
    std::atomic<bool> _locked{false};
    std::atomic<float> _evm_db{0.0f};
    std::atomic<float> _lock_metric{0.0f};
};
