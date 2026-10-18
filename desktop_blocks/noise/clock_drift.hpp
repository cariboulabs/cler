#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

// Sample-clock mismatch between a transmitter and a receiver that do not share a clock.
// Output sample n is the input evaluated at t_n = tau0 + n * r input samples, with
// r = 1 + ppm * 1e-6, i.e. ppm = (fs_tx / fs_rx - 1) * 1e6: positive ppm means the
// TX clock is fast, so the RX takes fewer samples per symbol (N inputs give ~N / r outputs).
// tau0 in [0, 1) is the initial fractional timing offset. Cubic (4-tap Lagrange)
// interpolation; at ppm 0 and tau0 0 the output equals the input sample for sample.
struct ClockDriftBlock : public cler::BlockBase {
    using Sample = std::complex<float>;
    cler::Channel<Sample> in;

    ClockDriftBlock(const char* name, const double ppm, const double timing_offset = 0.0,
        const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(Sample) : buffer_size),
          _ratio(1.0 + ppm * 1e-6), _timing_offset(timing_offset), _t(timing_offset)
    {
        if (buffer_size > 0 && buffer_size * sizeof(Sample) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (!(timing_offset >= 0.0 && timing_offset < 1.0)) {
            cler::panic("Timing offset must be in [0, 1) samples");
        }
        if (!(_ratio > 0.5 && _ratio < 2.0)) {
            cler::panic("ppm out of range");
        }
        _pending_ppm.store(ppm, std::memory_order_relaxed);
        _pending_offset.store(timing_offset, std::memory_order_relaxed);
        _time.store(timing_offset, std::memory_order_relaxed);
    }

    // Thread-safe: applied at the top of the next procedure().
    void set_ppm(double ppm) {
        _pending_ppm.store(ppm, std::memory_order_relaxed);
        _ppm_dirty.store(true, std::memory_order_release);
    }

    // Thread-safe: applied at the top of the next procedure(). Moves the sampling
    // instant by the change in offset, so the output steps by that fraction once.
    void set_timing_offset(double timing_offset) {
        _pending_offset.store(timing_offset, std::memory_order_relaxed);
        _offset_dirty.store(true, std::memory_order_release);
    }

    // Input-sample time of the next output sample, counted from the first input sample.
    double accumulated_time() const { return _time.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Sample>* out) {
        if (_ppm_dirty.exchange(false, std::memory_order_acquire)) {
            const double ratio = 1.0 + _pending_ppm.load(std::memory_order_relaxed) * 1e-6;
            if (ratio > 0.5 && ratio < 2.0) _ratio = ratio;
        }
        if (_offset_dirty.exchange(false, std::memory_order_acquire)) {
            const double offset = _pending_offset.load(std::memory_order_relaxed);
            if (offset >= 0.0 && offset < 1.0) {
                _t += offset - _timing_offset;
                _timing_offset = offset;
            }
        }

        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;

        // _t is relative to rptr[0]; indices -3..-1 live in _hist. Consuming up to
        // floor(_t) + 1 leaves _t >= -1, and an offset change moves it by > -1, so the
        // oldest tap (k - 1) never reaches past the history.
        const Sample* src = rptr;
        auto x = [this, src](long i) -> Sample { return i < 0 ? _hist[3 + i] : src[i]; };
        const long n_in = static_cast<long>(rsize);

        size_t produced = 0;
        while (produced < wsize) {
            const double fk = std::floor(_t);
            const long k = static_cast<long>(fk);
            if (k + 2 >= n_in) break;
            wptr[produced++] = interpolate(x(k - 1), x(k), x(k + 1), x(k + 2),
                static_cast<float>(_t - fk));
            _t += _ratio;
        }

        const long keep_from = static_cast<long>(std::floor(_t)) + 1;
        const long consumed = std::max(0L, std::min(n_in, keep_from));
        std::array<Sample, 3> hist;
        for (long j = 0; j < 3; ++j) hist[j] = x(consumed - 3 + j);
        _hist = hist;
        _t -= static_cast<double>(consumed);
        _consumed += static_cast<uint64_t>(consumed);
        _time.store(static_cast<double>(_consumed) + _t, std::memory_order_relaxed);

        in.commit_read(static_cast<size_t>(consumed));
        out->commit_write(produced);
        // Only reachable on a short read span (a lower bound): the next call refreshes it.
        if (produced == 0 && consumed == 0) return cler::Error::NotEnoughSamples;
        return cler::Empty{};
    }

private:
    // Lagrange cubic through x[k-1..k+2], evaluated at k + mu, mu in [0, 1).
    static Sample interpolate(Sample xm1, Sample x0, Sample x1, Sample x2, float mu) {
        const float cm1 = -mu * (mu - 1.0f) * (mu - 2.0f) / 6.0f;
        const float c0 = (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) / 2.0f;
        const float c1 = -(mu + 1.0f) * mu * (mu - 2.0f) / 2.0f;
        const float c2 = (mu + 1.0f) * mu * (mu - 1.0f) / 6.0f;
        return cm1 * xm1 + c0 * x0 + c1 * x1 + c2 * x2;
    }

    double _ratio;
    double _timing_offset;
    double _t;
    uint64_t _consumed = 0;
    std::array<Sample, 3> _hist{};

    std::atomic<double> _pending_ppm{0.0};
    std::atomic<double> _pending_offset{0.0};
    std::atomic<bool> _ppm_dirty{false};
    std::atomic<bool> _offset_dirty{false};
    std::atomic<double> _time{0.0};
};
