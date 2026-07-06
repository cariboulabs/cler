#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// Polyphase channelizer panel: splits the full I/Q stream into N equal-width
// channels (N = rate / channel_width, snapped to an integer in [2, 64]) with a
// maximally-decimated liquid firpfbch analyzer, and renders one scrolling
// power-vs-time strip per channel -- N mini zero-span scopes sharing one time
// axis -- in its own "Channelizer" window.
//
// Design notes:
//  * The block is a SINK: it always drains its input (even when hidden via
//    set_active(false)) so it can never back up the shared fanout and perturb
//    the trigger path.
//  * All liquid objects are created AND destroyed on the GUI thread through
//    the same staged pending/active/retired slot scheme as PowerDetectorBlock
//    (see power_detector.hpp): set_config() designs+stages, procedure() only
//    pointer-swaps under the mutex, the next set_config() (or the destructor)
//    frees the retired object. procedure() never allocates or frees.
//  * Storage is allocated ONCE in the constructor for the N_MAX x POINTS_MAX
//    worst case (64 * 2048 floats = 512 KB per ring copy); a channel-count
//    change just reinterprets/clears it -- no reallocation ever.
//  * HOT-PATH CPU RULE: |x|^2 is accumulated (peak-held) in LINEAR power per
//    channel-sample (aggregate channel-sample rate == input rate, e.g. 20 M/s)
//    and converted to dB only when a display bin completes -- points/s is a
//    few thousand at most, so log10f never runs at the sample rate.
//
// Calibration: firpfbch_crcf_create_kaiser() would feed UNNORMALIZED
// liquid_firdes_kaiser taps straight into the bank (fc = 0.5/N, DC gain
// sum(h) ~= N, i.e. an N-DEPENDENT +20*log10(N) level offset -- verified in
// liquid's src/multichannel/src/firpfbch.proto.c). We instead design the same
// Kaiser prototype ourselves and scale it to unity DC gain before
// firpfbch_crcf_create(), so an in-channel CW of amplitude A reads
// ~20*log10(A) dB in every channel REGARDLESS of N: changing the channel
// width never jumps the displayed levels.
//
// Channel k (FFT ordering, verified against liquid's analyzer: forward DFT of
// the polyphase branch outputs, demonstrated in
// examples/firpfbch_crcf_analysis_example.c) is centered at
//   f_k = k * rate / N          for k <= N/2
//   f_k = (k - N) * rate / N    for k >  N/2
// relative to the device center frequency. The strips are drawn bottom-to-top
// in ascending absolute frequency.
struct ChannelizerPanelBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    static constexpr size_t N_MIN      = 2;
    static constexpr size_t N_MAX      = 64;
    static constexpr size_t POINTS_MAX = 2048;   // dB points kept per strip
    // Upper bound on analyzer frames per procedure() call: bounds worst-case
    // per-call latency without ever letting the input channel back up (the
    // scheduler simply calls us again).
    static constexpr size_t FRAME_BUDGET = 256;
    static constexpr unsigned int FILTER_SEMI_LENGTH = 4;   // symbols (m)
    static constexpr float KAISER_ATTEN_DB = 60.0f;
    // Fixed strip display range: each strip band maps [DB_MIN, DB_MAX] into
    // 90% of its unit-height row (relative-level display; see render()).
    static constexpr float DB_MIN = -120.0f;
    static constexpr float DB_MAX = 0.0f;

    ChannelizerPanelBlock(const char* name, size_t buffer_size = 32768)
        : BlockBase(name), in(buffer_size)
    {
        // Everything sized once for the worst case; ~1.2 MB total.
        _rings      = new float[N_MAX * POINTS_MAX];   // per-channel dB point rings
        _render_buf = new float[N_MAX * POINTS_MAX];   // GUI's chronological copy
        _render_x   = new float[POINTS_MAX];           // shared time axis
        _line_y     = new float[POINTS_MAX];           // one strip's offset polyline
        _in_scratch = new std::complex<float>[N_MAX * FRAME_BUDGET];
        _chan_out   = new std::complex<float>[N_MAX];  // one analyzer frame out
        _accum      = new float[N_MAX];                // linear-power peak per bin
        std::fill_n(_rings, N_MAX * POINTS_MAX, DB_MIN);
        std::fill_n(_accum, N_MAX, 0.0f);
    }

    ChannelizerPanelBlock(const ChannelizerPanelBlock&) = delete;
    ChannelizerPanelBlock& operator=(const ChannelizerPanelBlock&) = delete;

    ~ChannelizerPanelBlock() {
        delete[] _rings;
        delete[] _render_buf;
        delete[] _render_x;
        delete[] _line_y;
        delete[] _in_scratch;
        delete[] _chan_out;
        delete[] _accum;
        if (_active)  firpfbch_crcf_destroy(_active);
        if (_pending) firpfbch_crcf_destroy(_pending);
        if (_retired) firpfbch_crcf_destroy(_retired);
    }

    // Channel count for a requested width at a given rate:
    // N = clamp(round(rate/width), N_MIN, N_MAX). Static so the GUI panel can
    // mirror the snapping next to the width control.
    static size_t channels_for(double width_hz, double rate_hz) {
        if (width_hz <= 0.0 || rate_hz <= 0.0) return N_MIN;
        double n = std::round(rate_hz / width_hz);
        n = std::min(std::max(n, static_cast<double>(N_MIN)),
                     static_cast<double>(N_MAX));
        return static_cast<size_t>(n);
    }

    // The channel width actually achieved for a request: rate / N.
    static double effective_width(double width_hz, double rate_hz) {
        if (rate_hz <= 0.0) return 0.0;
        return rate_hz / static_cast<double>(channels_for(width_hz, rate_hz));
    }

    // Stage a new channelizer configuration. GUI-thread only. width_hz snaps
    // to rate/N (see channels_for); span_s is the strips' shared time-axis
    // depth; center_freq_hz labels the Y ticks (absolute channel centers).
    // N, the bin size and the point spacing travel to the DSP thread as ONE
    // generation, so procedure() never sees torn values.
    void set_config(double width_hz, double rate_hz, double span_s,
                    double center_freq_hz) {
        // Label-only params (render() runs on this same GUI thread).
        _gui_rate_hz   = rate_hz;
        _gui_center_hz = center_freq_hz;

        // Drain the retired slot (destruction always here, on the GUI thread).
        firpfbch_crcf retired = nullptr;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            retired  = _retired;
            _retired = nullptr;
        }
        if (retired) firpfbch_crcf_destroy(retired);

        const size_t N = channels_for(width_hz, rate_hz);
        // Display bin: one dB point per bin_samples channel samples, chosen so
        // POINTS_MAX points span ~span_s seconds at the channel rate rate/N.
        const double chan_rate = rate_hz / static_cast<double>(N);
        double bs = std::round(span_s * chan_rate
                               / static_cast<double>(POINTS_MAX));
        if (bs < 1.0) bs = 1.0;
        const size_t bin_samples = static_cast<size_t>(bs);
        const float  point_dt    = static_cast<float>(
            static_cast<double>(bin_samples) / chan_rate);

        if (N == _staged_N && bin_samples == _staged_bin &&
            point_dt == _staged_dt)
            return;   // same DSP grid; only the labels above changed
        _staged_N   = N;
        _staged_bin = bin_samples;
        _staged_dt  = point_dt;

        // Same Kaiser prototype firpfbch_crcf_create_kaiser() designs
        // (h_len = 2*N*m + 1, fc = 0.5/N), but normalized to unity DC gain
        // (see calibration note in the header comment). create() consumes the
        // first N*2m taps, exactly as create_kaiser() does internally.
        const unsigned int h_len =
            2 * static_cast<unsigned int>(N) * FILTER_SEMI_LENGTH + 1;
        std::vector<float> h(h_len);
        liquid_firdes_kaiser(h_len, 0.5f / static_cast<float>(N),
                             KAISER_ATTEN_DB, 0.0f, h.data());
        float sum = 0.0f;
        for (float t : h) sum += t;
        if (sum != 0.0f)
            for (float& t : h) t /= sum;
        firpfbch_crcf q = firpfbch_crcf_create(
            LIQUID_ANALYZER, static_cast<unsigned int>(N),
            2 * FILTER_SEMI_LENGTH, h.data());

        firpfbch_crcf stale_pending = nullptr;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            stale_pending = _pending;   // staged but never consumed: replace
            _pending      = q;
            _pending_N    = N;
            _pending_bin  = bin_samples;
            _pending_dt   = point_dt;
        }
        _gen.fetch_add(1, std::memory_order_release);
        if (stale_pending) firpfbch_crcf_destroy(stale_pending);
    }

    // Retune only the Y-tick labels (absolute channel centers). GUI-thread
    // only; no DSP reconfiguration.
    void set_center_freq(double center_freq_hz) { _gui_center_hz = center_freq_hz; }

    // Drain-without-work when the window is hidden (mirrors the spectrum/
    // spectrogram set_active pattern) so the fanout never stalls.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    // One-shot programmatic window rect: the next render() applies it with
    // ImGuiCond_Always and clears the request (same pattern as the other
    // spike views). GUI thread only.
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Swap in a staged config (rare, GUI-driven). Pointer moves only: the
        // old analyzer is parked in the retired slot for the GUI thread to
        // destroy; nothing is freed or allocated here.
        const uint64_t gen = _gen.load(std::memory_order_acquire);
        if (gen != _gen_applied) {
            {
                std::lock_guard<std::mutex> lk(_cfg_mutex);
                if (_pending) {
                    if (_active) _retired = _active;   // GUI drained this slot
                    _active  = _pending;
                    _pending = nullptr;
                    _N           = _pending_N;
                    _bin_samples = _pending_bin;
                    _point_dt    = _pending_dt;
                }
                _gen_applied = _gen.load(std::memory_order_relaxed);
            }
            // New channel grid: old history is meaningless (different centers
            // widths and bin spacing), so clear the rings HERE, on the thread
            // that owns ring writes, so the rings always match _snap_N. A
            // 512 KB fill on a rare reconfig is fine; never runs per-sample.
            _bin_count = 0;
            std::fill_n(_accum, N_MAX, 0.0f);
            {
                std::lock_guard<std::mutex> lk(_snap_mutex);
                std::fill_n(_rings, N_MAX * POINTS_MAX, DB_MIN);
                _ring_w    = 0;
                _ring_fill = 0;
                _snap_N    = _N;
                _snap_dt   = _point_dt;
            }
        }

        // Not yet configured, or hidden: drain everything so the shared
        // fanout can never back up, do no DSP work.
        if (!_active || _external_pause.load(std::memory_order_acquire)) {
            const size_t avail = in.size();
            if (avail == 0) return cler::Error::NotEnoughSamples;
            in.commit_read(avail);
            _bin_count = 0;
            std::fill_n(_accum, N_MAX, 0.0f);
            return cler::Empty{};
        }

        const size_t N = _N;
        const size_t frames = std::min(in.size() / N, FRAME_BUDGET);
        if (frames == 0) return cler::Error::NotEnoughSamples;
        in.readN(_in_scratch, frames * N);

        for (size_t f = 0; f < frames; ++f) {
            firpfbch_crcf_analyzer_execute(
                _active,
                reinterpret_cast<liquid_float_complex*>(_in_scratch + f * N),
                reinterpret_cast<liquid_float_complex*>(_chan_out));
            // Peak-hold LINEAR power per channel; no log10f here (this loop
            // runs at the full input rate in aggregate).
            for (size_t ch = 0; ch < N; ++ch) {
                const float re = _chan_out[ch].real();
                const float im = _chan_out[ch].imag();
                const float p  = re * re + im * im;
                if (p > _accum[ch]) _accum[ch] = p;
            }
            if (++_bin_count >= _bin_samples) flush_bin(N);
        }
        return cler::Empty{};
    }

    void render() {
        if (_pending_rect) {
            ImGui::SetNextWindowPos(_pending_rect_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(_pending_rect_size, ImGuiCond_Always);
            _pending_rect = false;
        } else {
            ImGui::SetNextWindowPos(_win_pos, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(_win_size, ImGuiCond_FirstUseEver);
        }
        ImGui::Begin(name());

        // Copy the rings into a render-local buffer, unrolled into
        // chronological order (two memcpys per channel). On lock contention
        // just redraw last frame's copy.
        if (_snap_mutex.try_lock()) {
            _r_N    = _snap_N;
            _r_fill = _ring_fill;
            _r_dt   = _snap_dt;
            const size_t fill  = _r_fill;
            const size_t start = (_ring_w + POINTS_MAX - fill) % POINTS_MAX;
            const size_t first = std::min(fill, POINTS_MAX - start);
            for (size_t ch = 0; ch < _r_N; ++ch) {
                const float* src = _rings      + ch * POINTS_MAX;
                float*       dst = _render_buf + ch * POINTS_MAX;
                std::memcpy(dst, src + start, first * sizeof(float));
                if (fill > first)
                    std::memcpy(dst + first, src, (fill - first) * sizeof(float));
            }
            _snap_mutex.unlock();
        }

        const size_t N    = _r_N;
        const size_t fill = _r_fill;
        const double span_s = static_cast<double>(POINTS_MAX)
                            * static_cast<double>(_r_dt);

        if (N == 0) {
            ImGui::TextUnformatted("Waiting for configuration...");
            ImGui::End();
            return;
        }

        const double eff_mhz = (_gui_rate_hz > 0.0)
            ? _gui_rate_hz / static_cast<double>(N) / 1e6 : 0.0;
        ImGui::Text("%zu channels x %.3f MHz | span %.1f s | strip range [%.0f, %.0f] dB",
                    N, eff_mhz, span_s,
                    static_cast<double>(DB_MIN), static_cast<double>(DB_MAX));

        rebuild_ticks_if_needed(N);

        if (ImPlot::BeginPlot("##strips", ImVec2(-1, -1), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Time [s]", "Channel center [MHz]");
            ImPlot::SetupAxisLimits(ImAxis_X1, -span_s, 0.0, ImPlotCond_Always);
            // Re-fit Y only when the strip count changes; otherwise the user
            // may zoom into a band of strips freely.
            ImPlot::SetupAxisLimits(ImAxis_Y1, -0.2,
                                    static_cast<double>(N) + 0.2,
                                    (_y_fit_N == N) ? ImPlotCond_Once
                                                    : ImPlotCond_Always);
            _y_fit_N = N;
            if (!_tick_pos.empty())
                ImPlot::SetupAxisTicks(ImAxis_Y1, _tick_pos.data(),
                                       static_cast<int>(_tick_pos.size()),
                                       _tick_ptrs.data(), false);

            if (fill > 0) {
                // Shared time axis: newest point at t = 0, older to the left.
                for (size_t j = 0; j < fill; ++j)
                    _render_x[j] = -static_cast<float>(fill - 1 - j) * _r_dt;

                // One polyline per strip, bottom row = lowest absolute
                // frequency. Row r shows FFT channel (r - N/2) mod N (the
                // verified analyzer ordering). Each strip normalizes
                // [DB_MIN, DB_MAX] into [row, row + 0.9]. Worst case
                // 64 * 2048 = 131k line points/frame -- fine for ImPlot.
                const float inv_range = 1.0f / (DB_MAX - DB_MIN);
                for (size_t r = 0; r < N; ++r) {
                    const size_t ch = (r + N - N / 2) % N;
                    const float* src = _render_buf + ch * POINTS_MAX;
                    const float  base = static_cast<float>(r);
                    for (size_t j = 0; j < fill; ++j) {
                        float frac = (src[j] - DB_MIN) * inv_range;
                        frac = std::min(std::max(frac, 0.0f), 1.0f);
                        _line_y[j] = base + 0.9f * frac;
                    }
                    char id[16];
                    std::snprintf(id, sizeof(id), "##s%u",
                                  static_cast<unsigned int>(r));   // r < N_MAX
                    ImPlot::PlotLine(id, _render_x, _line_y,
                                     static_cast<int>(fill));
                }
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    // Convert the completed bin's peak power to dB and push one point per
    // channel. Runs at the display point rate (a few kpoints/s at most), so
    // both the log10f and the brief lock are cheap.
    void flush_bin(size_t N) {
        std::lock_guard<std::mutex> lk(_snap_mutex);
        for (size_t ch = 0; ch < N; ++ch) {
            const float db = 10.0f * log10f(_accum[ch] + 1e-20f);
            _rings[ch * POINTS_MAX + _ring_w] = std::max(db, DB_MIN);
            _accum[ch] = 0.0f;
        }
        _ring_w = (_ring_w + 1) % POINTS_MAX;
        if (_ring_fill < POINTS_MAX) ++_ring_fill;
        _bin_count = 0;
    }

    // Y-tick label cache: rebuilt only when N / rate / center change, never
    // per frame. At high N the ticks are strided so labels stay readable.
    void rebuild_ticks_if_needed(size_t N) {
        if (N == _labels_N && _gui_rate_hz == _labels_rate &&
            _gui_center_hz == _labels_center)
            return;
        _labels_N      = N;
        _labels_rate   = _gui_rate_hz;
        _labels_center = _gui_center_hz;
        _tick_pos.clear();
        _tick_labels.clear();
        _tick_ptrs.clear();
        const double df = (N > 0) ? _gui_rate_hz / static_cast<double>(N) : 0.0;
        const size_t stride = (N + 31) / 32;   // at most ~32 labeled strips
        char buf[32];
        for (size_t r = 0; r < N; r += stride) {
            // Row r (bottom = lowest freq) is FFT channel k with signed index
            // r - N/2, centered at center + (r - N/2) * rate/N.
            const double off = (static_cast<double>(r)
                                - static_cast<double>(N / 2)) * df;
            std::snprintf(buf, sizeof(buf), "%.3f", (_gui_center_hz + off) / 1e6);
            _tick_pos.push_back(static_cast<double>(r) + 0.45);
            _tick_labels.emplace_back(buf);
        }
        _tick_ptrs.reserve(_tick_labels.size());
        for (const std::string& s : _tick_labels)
            _tick_ptrs.push_back(s.c_str());
    }

    // ---- fixed storage (allocated once; see constructor) ----
    float*               _rings      = nullptr;  // [N_MAX][POINTS_MAX] dB, DSP under _snap_mutex
    float*               _render_buf = nullptr;  // GUI chronological copy
    float*               _render_x   = nullptr;  // shared strip time axis
    float*               _line_y     = nullptr;  // one strip's offset polyline
    std::complex<float>* _in_scratch = nullptr;  // FRAME_BUDGET analyzer frames
    std::complex<float>* _chan_out   = nullptr;  // one sample per channel
    float*               _accum      = nullptr;  // linear peak power per channel

    // ---- analyzer slots (create/destroy on GUI thread; procedure() only
    //      moves pointers under _cfg_mutex -- see power_detector.hpp) ----
    std::mutex    _cfg_mutex;
    firpfbch_crcf _active  = nullptr;   // owned by the DSP thread's hot path
    firpfbch_crcf _pending = nullptr;   // staged by GUI, not yet consumed
    firpfbch_crcf _retired = nullptr;   // swapped out; awaiting GUI destroy
    size_t        _pending_N   = 0;
    size_t        _pending_bin = 1;
    float         _pending_dt  = 0.0f;

    std::atomic<uint64_t> _gen{0};
    uint64_t              _gen_applied = 0;
    std::atomic<bool>     _external_pause{false};

    // ---- DSP-thread copies of the applied config ----
    size_t _N           = 0;
    size_t _bin_samples = 1;
    float  _point_dt    = 0.0f;
    size_t _bin_count   = 0;

    // ---- ring snapshot shared with the GUI (guarded by _snap_mutex) ----
    std::mutex _snap_mutex;
    size_t     _ring_w    = 0;      // next point slot (shared by all channels)
    size_t     _ring_fill = 0;      // valid points (saturates at POINTS_MAX)
    size_t     _snap_N    = 0;      // N the rings correspond to
    float      _snap_dt   = 0.0f;   // seconds per ring point

    // ---- GUI-thread state (setters + render() share the GUI thread) ----
    size_t _staged_N   = 0;         // last staged grid (skip no-op restages)
    size_t _staged_bin = 0;
    float  _staged_dt  = -1.0f;
    double _gui_rate_hz   = 0.0;    // label-only params
    double _gui_center_hz = 0.0;
    size_t _r_N    = 0;             // last snapshot copied for rendering
    size_t _r_fill = 0;
    float  _r_dt   = 0.0f;
    size_t _y_fit_N = 0;            // strip count the Y axis was last fit for

    size_t _labels_N = 0;           // tick-label cache keys
    double _labels_rate = -1.0, _labels_center = -1.0;
    std::vector<double>      _tick_pos;
    std::vector<std::string> _tick_labels;
    std::vector<const char*> _tick_ptrs;

    ImVec2 _win_pos{380.0f, 455.0f};
    ImVec2 _win_size{1100.0f, 430.0f};
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos{0.0f, 0.0f};
    ImVec2 _pending_rect_size{0.0f, 0.0f};
};
