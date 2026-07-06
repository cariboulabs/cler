#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <vector>

// Converts complex I/Q samples to instantaneous power in dB, optionally after
// an integer DECIMATING channel selector (anti-alias lowpass + downsample).
//
// Backpressure-safe: it only ever processes as many samples as can fit in the
// output channel this call (min of input available, output space, scratch
// size). It never calls a blocking push() and never consumes input it cannot
// emit, so a slow downstream consumer simply throttles it instead of corrupting
// or dropping samples.
//
// Channel bandwidth: by default the detection bandwidth equals the full sample
// rate (bypass -- identical to the historical behavior). Calling
// set_channel_bandwidth() from the GUI thread stages a firdecim_crcf decimator
// with factor R ~= rate/bw whose fc = 0.5/R Kaiser anti-alias filter (80 dB
// stopband) IS the channel selection: the effective two-sided bandwidth is
// exactly rate/R and out-of-band energy is rejected before the power
// computation. The output (detection) stream then runs at rate/R.
//
// Why decimate instead of filtering at the full rate: a polyphase decimator
// only computes the outputs it keeps, so the cost is ~(2*m + 1/R) MACs per
// INPUT sample regardless of how narrow the channel is (m = FILTER_SEMI_LENGTH
// below), and the per-sample log10f runs at the decimated rate. A same-quality
// non-decimating lowpass at narrow cutoffs needs thousands of taps at the full
// rate and saturates the DSP thread.
//
// Threading/lifecycle (all liquid objects created AND destroyed on the GUI
// thread; procedure() never allocates or frees):
//   - set_channel_bandwidth() designs the taps on the heap, creates the new
//     firdecim, and stages it in _pending under _cfg_mutex, bumping _gen.
//   - procedure() notices the generation change and, under the mutex, moves
//     active->retired and pending->active (pointer swaps only).
//   - the NEXT set_channel_bandwidth() call (and the destructor) destroys
//     whatever sits in the retired slot, plus any never-consumed pending.
template<typename T = std::complex<float>>
struct PowerDetectorBlock : public cler::BlockBase {
    static_assert(std::is_same<T, std::complex<float>>::value,
                  "channel-selector path requires std::complex<float> samples");

    cler::Channel<T> in;

    // Anti-alias filter semi-length in OUTPUT samples: h_len = 2*R*m + 1 taps,
    // group delay = m output samples (negligible for a power trace).
    static constexpr unsigned int FILTER_SEMI_LENGTH = 12;
    // Hard cap on the decimation factor. CRITICAL deadlock ceiling: the input
    // channel is buffer_size (32768) samples deep and procedure() must gather
    // R inputs per output; R must stay well below that or the block could
    // never make progress and would stall the shared fanout. 4096 keeps us 8x
    // under the default depth.
    static constexpr size_t MAX_DECIMATION = 4096;
    // Output (detection) rate floor: R is chosen so rate/R >= this.
    static constexpr double MIN_OUTPUT_RATE_HZ = 10e3;

    PowerDetectorBlock(const char* name,
                       float min_power_db = -100.0f,  // Clip anything below this
                       size_t buffer_size = 32768)
        : BlockBase(name),
          in(buffer_size),
          _min_power_db(min_power_db),
          _scratch_size(buffer_size)
    {
        _in_scratch   = new T[_scratch_size];
        _filt_scratch = new T[_scratch_size];
        _out_scratch  = new float[_scratch_size];
    }

    PowerDetectorBlock(const PowerDetectorBlock&) = delete;
    PowerDetectorBlock& operator=(const PowerDetectorBlock&) = delete;

    ~PowerDetectorBlock() {
        delete[] _in_scratch;
        delete[] _filt_scratch;
        delete[] _out_scratch;
        if (_active)  firdecim_crcf_destroy(_active);
        if (_pending) firdecim_crcf_destroy(_pending);
        if (_retired) firdecim_crcf_destroy(_retired);
    }

    // Decimation factor for a requested two-sided bandwidth at a given rate:
    // R = clamp(round(rate/bw), 1, min(floor(rate/MIN_OUTPUT_RATE_HZ),
    // MAX_DECIMATION)). Static so the GUI panel can mirror the snapping.
    static size_t decimation_for(double bw_hz, double rate_hz) {
        if (rate_hz <= 0.0 || bw_hz <= 0.0) return 1;
        double r_max = std::floor(rate_hz / MIN_OUTPUT_RATE_HZ);
        if (r_max > static_cast<double>(MAX_DECIMATION))
            r_max = static_cast<double>(MAX_DECIMATION);
        if (r_max < 1.0) r_max = 1.0;
        double r = std::round(rate_hz / bw_hz);
        r = std::min(std::max(r, 1.0), r_max);
        return static_cast<size_t>(r);
    }

    // The bandwidth actually achieved for a request (two-sided passband of
    // the fc = 0.5/R anti-alias filter): rate / R.
    static double effective_bandwidth(double bw_hz, double rate_hz) {
        if (rate_hz <= 0.0) return 0.0;
        return rate_hz / static_cast<double>(decimation_for(bw_hz, rate_hz));
    }

    // Most recently STAGED decimation factor (GUI-side view; the DSP thread
    // picks it up at the top of its next procedure() call).
    size_t decimation() const { return _decim_staged.load(std::memory_order_acquire); }

    // Stage a new channel (detection) bandwidth. GUI-thread only. bw_hz is the
    // TWO-SIDED bandwidth around DC; the request snaps to rate/R for integer R
    // (see decimation_for). R == 1 bypasses the filter entirely.
    void set_channel_bandwidth(double bw_hz, double sample_rate_hz) {
        const size_t R = decimation_for(bw_hz, sample_rate_hz);

        // Drain the retired slot (an active object procedure() swapped out
        // earlier) -- destruction always happens HERE, on the GUI thread.
        firdecim_crcf retired = nullptr;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            retired  = _retired;
            _retired = nullptr;
        }
        if (retired) firdecim_crcf_destroy(retired);

        if (R == _requested_R) return;   // taps depend only on R; nothing to do
        _requested_R = R;

        // Design the new decimator on the heap (firdecim_crcf_create_kaiser
        // would put two h_len VLAs on the stack -- ~3.8 MB at large R -- so we
        // design the taps ourselves). liquid_firdes_kaiser taps are
        // UNNORMALIZED (DC gain ~= 2*fc*h_len != 1); scale by 1/sum(taps) for
        // unity DC gain so the reported power level is calibrated.
        firdecim_crcf q = nullptr;
        if (R >= 2) {
            const unsigned int h_len =
                2 * static_cast<unsigned int>(R) * FILTER_SEMI_LENGTH + 1;
            std::vector<float> h(h_len);
            liquid_firdes_kaiser(h_len, 0.5f / static_cast<float>(R),
                                 80.0f, 0.0f, h.data());
            float sum = 0.0f;
            for (float t : h) sum += t;
            q = firdecim_crcf_create(static_cast<unsigned int>(R),
                                     h.data(), h_len);
            if (sum != 0.0f) firdecim_crcf_set_scale(q, 1.0f / sum);
        }

        firdecim_crcf stale_pending = nullptr;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            stale_pending   = _pending;   // staged but never consumed: replace
            _pending        = q;
            _pending_R      = R;
            _pending_bypass = (R == 1);
        }
        _gen.fetch_add(1, std::memory_order_release);
        _decim_staged.store(R, std::memory_order_release);
        if (stale_pending) firdecim_crcf_destroy(stale_pending);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out_base) {
        auto* out = static_cast<cler::Channel<float>*>(out_base);

        // Swap in a staged decimator (rare, GUI-driven). Pointer moves only:
        // the old active object is parked in the retired slot for the GUI
        // thread to destroy; nothing is freed or allocated here.
        uint64_t gen = _gen.load(std::memory_order_acquire);
        if (gen != _gen_applied) {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            if (_active) _retired = _active;   // GUI drained this slot before staging
            _active  = _pending;
            _pending = nullptr;
            _decim   = _pending_R;
            _bypass  = _pending_bypass;
            _gen_applied = _gen.load(std::memory_order_relaxed);
        }

        if (_bypass) {
            size_t transferable = std::min({in.size(), out->space(), _scratch_size});
            if (transferable == 0) {
                // Distinguish so the scheduler can back off appropriately.
                return (in.size() == 0) ? cler::Error::NotEnoughSamples
                                        : cler::Error::NotEnoughSpace;
            }
            in.readN(_in_scratch, transferable);
            compute_power(_in_scratch, transferable);
            out->writeN(_out_scratch, transferable);
            return cler::Empty{};
        }

        const size_t R = _decim;
        size_t n_out = std::min({in.size() / R, out->space(), _scratch_size / R});
        if (n_out == 0) {
            return (in.size() < R) ? cler::Error::NotEnoughSamples
                                   : cler::Error::NotEnoughSpace;
        }
        in.readN(_in_scratch, n_out * R);
        firdecim_crcf_execute_block(
            _active,
            reinterpret_cast<liquid_float_complex*>(_in_scratch),
            static_cast<unsigned int>(n_out),
            reinterpret_cast<liquid_float_complex*>(_filt_scratch));
        compute_power(_filt_scratch, n_out);
        out->writeN(_out_scratch, n_out);

        return cler::Empty{};
    }

private:
    void compute_power(const T* src, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            float re = src[i].real();
            float im = src[i].imag();
            float power_linear = re * re + im * im;
            float power_db = 10.0f * log10f(power_linear + 1e-20f);
            _out_scratch[i] = std::max(power_db, _min_power_db);
        }
    }

    float  _min_power_db;
    size_t _scratch_size;
    T*     _in_scratch   = nullptr;
    T*     _filt_scratch = nullptr;   // decimator output (channel-selector path)
    float* _out_scratch  = nullptr;

    // Decimator slots. All create/destroy on the GUI thread; procedure() only
    // moves pointers between slots under _cfg_mutex (see header comment).
    std::mutex    _cfg_mutex;
    firdecim_crcf _active  = nullptr;   // owned by the DSP thread's hot path
    firdecim_crcf _pending = nullptr;   // staged by GUI, not yet consumed
    firdecim_crcf _retired = nullptr;   // swapped out; awaiting GUI destroy
    size_t        _pending_R      = 1;
    bool          _pending_bypass = true;

    // DSP-thread copies of the applied config (plain members: only touched by
    // procedure()).
    size_t _decim  = 1;
    bool   _bypass = true;

    std::atomic<uint64_t> _gen{0};
    uint64_t              _gen_applied = 0;

    size_t                _requested_R = 1;    // GUI-thread only
    std::atomic<size_t>   _decim_staged{1};
};
