#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/plots/spectral_windows.hpp"
#include "liquid.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// One averaged power spectrum, dB quantised to u8 on a fixed scale so a
// consumer can draw an axis without autoscale: dB = db_min + bins[i] * db_step.
// Fixed-size so it is a POD channel element; n <= MAX_N bins are valid.
struct SpectrumFrame {
    static constexpr size_t MAX_N = 4096;
    uint32_t gen;
    double center_hz;
    double rate_hz;
    uint16_t n;
    float db_min, db_step;
    uint8_t bins[MAX_N];
};

// Headless spectrum: FFT in procedure(), window + power average over up to
// `avg` consecutive frames, at most `fps` frames per second. A tap off a
// fanout: it drains its input every call and never backpressures the chain.
struct SpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    SpectrumBlock(const char* name, double rate_hz, size_t n_fft = 1024, float fps = 20.0f,
                  float db_min = -120.0f, float db_step = 0.5f, size_t avg = 4,
                  SpectralWindow window = SpectralWindow::Hann, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _n(n_fft), _avg(avg == 0 ? 1 : avg), _db_min(db_min), _db_step(db_step),
          _min_interval(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / (fps > 0.0f ? fps : 20.0f)))),
          _rate(rate_hz), _center(0.0), _gen(0),
          _buf(n_fft), _window(n_fft), _power(n_fft)
    {
        if (n_fft < 16 || n_fft > SpectrumFrame::MAX_N || (n_fft & (n_fft - 1)) != 0) {
            cler::panic("SpectrumBlock: n_fft must be a power of two in [16, 4096]");
        }
        if (db_step <= 0.0f) cler::panic("SpectrumBlock: db_step must be positive");
        if (in.space() < n_fft) cler::panic("SpectrumBlock: input buffer smaller than n_fft");
        float gain = 0.0f;
        for (size_t i = 0; i < n_fft; ++i) {
            _window[i] = spectral_window_function(window, static_cast<float>(i) / static_cast<float>(n_fft - 1));
            gain += _window[i];
        }
        _scale2 = gain * gain;
        _plan = fft_create_plan(static_cast<unsigned int>(n_fft),
                                reinterpret_cast<liquid_float_complex*>(_buf.data()),
                                reinterpret_cast<liquid_float_complex*>(_buf.data()),
                                LIQUID_FFT_FORWARD, 0);
        if (!_plan) cler::panic("SpectrumBlock: fft_create_plan failed");
        _last = std::chrono::steady_clock::now() - _min_interval;
    }

    ~SpectrumBlock() { if (_plan) fft_destroy_plan(_plan); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<SpectrumFrame>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;

        const auto now = std::chrono::steady_clock::now();
        if (rsize >= _n && now - _last >= _min_interval) {
            auto [wptr, wsize] = out->write_dbf();
            if (wsize > 0) {
                const size_t frames = std::min(_avg, rsize / _n);
                const std::complex<float>* tail = rptr + rsize - frames * _n;
                std::fill(_power.begin(), _power.end(), 0.0f);
                for (size_t f = 0; f < frames; ++f) {
                    for (size_t i = 0; i < _n; ++i) _buf[i] = tail[f * _n + i] * _window[i];
                    fft_execute(_plan);
                    for (size_t i = 0; i < _n; ++i) _power[i] += std::norm(_buf[i]);
                }
                const float norm = 1.0f / (_scale2 * static_cast<float>(frames));
                SpectrumFrame& fr = wptr[0];
                fr.gen = _gen.load(std::memory_order_relaxed);
                fr.center_hz = _center.load(std::memory_order_relaxed);
                fr.rate_hz = _rate.load(std::memory_order_relaxed);
                fr.n = static_cast<uint16_t>(_n);
                fr.db_min = _db_min;
                fr.db_step = _db_step;
                const size_t half = _n / 2;
                for (size_t i = 0; i < _n; ++i) {
                    const float db = 10.0f * std::log10(_power[(i + half) % _n] * norm + 1e-20f);
                    const float q = (db - _db_min) / _db_step;
                    fr.bins[i] = static_cast<uint8_t>(q <= 0.0f ? 0.0f : q >= 255.0f ? 255.0f : q + 0.5f);
                }
                out->commit_write(1);
                _last = now;
            }
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    void set_rate(double hz) { _rate.store(hz, std::memory_order_relaxed); }
    void set_center(double hz) { _center.store(hz, std::memory_order_relaxed); }
    void set_gen(uint32_t gen) { _gen.store(gen, std::memory_order_relaxed); }
    size_t n_fft() const { return _n; }

private:
    size_t _n, _avg;
    float _db_min, _db_step, _scale2 = 1.0f;
    std::chrono::steady_clock::duration _min_interval;
    std::chrono::steady_clock::time_point _last;
    std::atomic<double> _rate, _center;
    std::atomic<uint32_t> _gen;
    std::vector<std::complex<float>> _buf;
    std::vector<float> _window, _power;
    fftplan _plan = nullptr;
};
