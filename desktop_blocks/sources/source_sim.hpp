#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <random>
#include <thread>

// A stand-in SDR: one complex tone at `tone_hz` above centre, in white noise,
// paced to real time so the rest of the graph behaves as with hardware.
// Centre and rate are what a radio would report; tone_hz is relative to centre.
struct SimSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SimSourceBlock(const char* name, double rate_hz, double center_hz = 100e6,
                   double tone_hz = 100e3, float snr_db = 30.0f)
        : cler::BlockBase(name), _rate(rate_hz), _center(center_hz),
          _tone(tone_hz), _snr_db(snr_db), _rng(12345)
    {
        if (rate_hz <= 0.0) cler::panic("SimSourceBlock: rate must be positive");
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, space] = out->write_dbf();
        if (space == 0) return cler::Error::NotEnoughSpace;

        const double rate = _rate.load(std::memory_order_relaxed);
        if (!_started) { _epoch = clock::now(); _emitted = 0; _started = true; }
        size_t due = samples_due(rate);
        if (due <= _emitted) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            due = samples_due(rate);
            if (due <= _emitted) return cler::Error::NotEnoughSamples;
        }
        const size_t n = std::min(space, due - _emitted);

        const double inc = 2.0 * cler::PI * _tone.load(std::memory_order_relaxed) / rate;
        const std::complex<double> rot = std::polar(1.0, inc);
        const float sigma = std::pow(10.0f, -_snr_db.load(std::memory_order_relaxed) / 20.0f) / std::sqrt(2.0f);
        for (size_t i = 0; i < n; ++i) {
            wptr[i] = {static_cast<float>(_phasor.real()) + sigma * _gauss(_rng),
                       static_cast<float>(_phasor.imag()) + sigma * _gauss(_rng)};
            _phasor *= rot;
        }
        _phasor /= std::abs(_phasor);
        out->commit_write(n);
        _emitted += n;
        if (due - _emitted > rate / 10.0) _epoch = clock::now() - to_duration(_emitted, rate);
        return cler::Empty{};
    }

    double rate() const { return _rate.load(std::memory_order_relaxed); }
    double center() const { return _center.load(std::memory_order_relaxed); }
    double tone_hz() const { return _tone.load(std::memory_order_relaxed); }
    float snr_db() const { return _snr_db.load(std::memory_order_relaxed); }
    // Graph stopped only: restarts the pacing epoch.
    void set_rate(double hz) {
        if (hz <= 0.0) cler::panic("SimSourceBlock: rate must be positive");
        _rate.store(hz, std::memory_order_relaxed);
        _started = false;
    }
    void set_center(double hz) { _center.store(hz, std::memory_order_relaxed); }
    void set_tone_hz(double hz) { _tone.store(hz, std::memory_order_relaxed); }
    void set_snr_db(float db) { _snr_db.store(db, std::memory_order_relaxed); }

private:
    using clock = std::chrono::steady_clock;

    size_t samples_due(double rate) const {
        return static_cast<size_t>(std::chrono::duration<double>(clock::now() - _epoch).count() * rate);
    }
    static clock::duration to_duration(size_t samples, double rate) {
        return std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(samples / rate));
    }

    std::atomic<double> _rate, _center, _tone;
    std::atomic<float> _snr_db;
    std::complex<double> _phasor{1.0, 0.0};
    std::mt19937 _rng;
    std::normal_distribution<float> _gauss{0.0f, 1.0f};
    bool _started = false;
    size_t _emitted = 0;
    clock::time_point _epoch;
};
