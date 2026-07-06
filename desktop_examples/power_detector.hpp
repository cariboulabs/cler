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

// Converts complex I/Q samples to instantaneous power in dB, optionally after
// a complex lowpass ("video bandwidth") filter.
//
// Backpressure-safe: it only ever processes as many samples as can fit in the
// output channel this call (min of input available, output space, scratch
// size). It never calls a blocking push() and never consumes input it cannot
// emit, so a slow downstream consumer simply throttles it instead of corrupting
// or dropping samples.
//
// Video bandwidth: by default the detection bandwidth equals the full sample
// rate (bypass -- identical to the historical behavior). Calling
// set_video_bandwidth() from the GUI thread stages a Kaiser-windowed FIR
// lowpass (fixed FILTER_TAPS taps, 60 dB stopband) that procedure() applies to
// the I/Q stream BEFORE the power computation. Tap design happens on the GUI
// thread; the DSP thread only swaps taps in via firfilt_crcf_recreate() (no
// reallocation since the tap count never changes), so procedure() stays
// allocation-free.
template<typename T = std::complex<float>>
struct PowerDetectorBlock : public cler::BlockBase {
    static_assert(std::is_same<T, std::complex<float>>::value,
                  "video-bandwidth filter path requires std::complex<float> samples");

    cler::Channel<T> in;

    static constexpr unsigned int FILTER_TAPS = 129;

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

        // Create the filter once with passthrough taps (unit impulse at tap 0,
        // so no group delay) and start in bypass: out-of-the-box behavior is
        // identical to the unfiltered detector.
        for (unsigned int i = 0; i < FILTER_TAPS; ++i) _taps_pending[i] = 0.0f;
        _taps_pending[0] = 1.0f;
        _filt = firfilt_crcf_create(_taps_pending, FILTER_TAPS);
    }

    PowerDetectorBlock(const PowerDetectorBlock&) = delete;
    PowerDetectorBlock& operator=(const PowerDetectorBlock&) = delete;

    ~PowerDetectorBlock() {
        delete[] _in_scratch;
        delete[] _filt_scratch;
        delete[] _out_scratch;
        firfilt_crcf_destroy(_filt);
    }

    // Stage a new video (detection) bandwidth. GUI-thread only; the DSP thread
    // picks the taps up at the top of the next procedure() call.
    // bw_hz is the TWO-SIDED detection bandwidth around DC; a complex lowpass
    // with cutoff bw_hz/2 passes exactly that span. bw_hz >= sample_rate means
    // "no filtering" (bypass). The normalized cutoff is clamped to liquid's
    // strict (0, 0.5) design range, so very small requests saturate at
    // 0.002 * sample_rate (two-sided).
    void set_video_bandwidth(double bw_hz, double sample_rate_hz) {
        float taps[FILTER_TAPS];
        bool bypass = (sample_rate_hz <= 0.0) || (bw_hz >= sample_rate_hz);
        if (!bypass) {
            double fc = (bw_hz / 2.0) / sample_rate_hz;   // one-sided, normalized
            fc = std::min(std::max(fc, 1e-3), 0.4999);
            liquid_firdes_kaiser(FILTER_TAPS, static_cast<float>(fc),
                                 60.0f, 0.0f, taps);
        } else {
            for (unsigned int i = 0; i < FILTER_TAPS; ++i) taps[i] = 0.0f;
            taps[0] = 1.0f;
        }
        {
            std::lock_guard<std::mutex> lk(_taps_mutex);
            std::copy(taps, taps + FILTER_TAPS, _taps_pending);
            _bypass_pending = bypass;
        }
        _taps_gen.fetch_add(1, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out_base) {
        auto* out = static_cast<cler::Channel<float>*>(out_base);

        // Swap in staged filter taps (rare, GUI-driven). recreate() with an
        // unchanged tap count does not reallocate, so this stays hot-path safe.
        uint64_t gen = _taps_gen.load(std::memory_order_acquire);
        if (gen != _taps_applied) {
            std::lock_guard<std::mutex> lk(_taps_mutex);
            firfilt_crcf_recreate(_filt, _taps_pending, FILTER_TAPS);
            _bypass       = _bypass_pending;
            _taps_applied = _taps_gen.load(std::memory_order_relaxed);
        }

        size_t transferable = std::min({in.size(), out->space(), _scratch_size});
        if (transferable == 0) {
            // Distinguish so the scheduler can back off appropriately.
            return (in.size() == 0) ? cler::Error::NotEnoughSamples
                                    : cler::Error::NotEnoughSpace;
        }

        in.readN(_in_scratch, transferable);
        const T* src = _in_scratch;
        if (!_bypass) {
            firfilt_crcf_execute_block(
                _filt,
                reinterpret_cast<liquid_float_complex*>(_in_scratch),
                static_cast<unsigned int>(transferable),
                reinterpret_cast<liquid_float_complex*>(_filt_scratch));
            src = _filt_scratch;
        }
        for (size_t i = 0; i < transferable; ++i) {
            float re = src[i].real();
            float im = src[i].imag();
            float power_linear = re * re + im * im;
            float power_db = 10.0f * log10f(power_linear + 1e-20f);
            _out_scratch[i] = std::max(power_db, _min_power_db);
        }
        out->writeN(_out_scratch, transferable);

        return cler::Empty{};
    }

private:
    float  _min_power_db;
    size_t _scratch_size;
    T*     _in_scratch   = nullptr;
    T*     _filt_scratch = nullptr;   // filter output (video-bandwidth path)
    float* _out_scratch  = nullptr;

    // Video-bandwidth filter. Staged from the GUI thread under _taps_mutex,
    // applied by the DSP thread when the generation counter changes.
    firfilt_crcf          _filt = nullptr;
    std::mutex            _taps_mutex;
    float                 _taps_pending[FILTER_TAPS];
    bool                  _bypass_pending = true;
    bool                  _bypass         = true;   // DSP-thread copy
    std::atomic<uint64_t> _taps_gen{0};
    uint64_t              _taps_applied = 0;
};
