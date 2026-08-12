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

// Polyphase channelizer: N-channel liquid firpfbch analyzer, N scrolling
// power-vs-time strips on a shared time axis, in a "Channelizer" window.
//
// SINK: procedure() always drains `in`, even while hidden, so it can never
// back up the shared fanout.
struct ChannelizerPanelBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    cler::Channel<std::complex<float>> in;

    static constexpr size_t N_MIN      = 2;
    static constexpr size_t N_MAX      = 64;
    static constexpr size_t POINTS_MAX = 2048;   // dB points kept per strip
    // Cap on analyzer frames per procedure() call: bounds per-call latency
    // without letting `in` back up (the scheduler just calls us again).
    static constexpr size_t FRAME_BUDGET = 256;
    static constexpr unsigned int FILTER_SEMI_LENGTH = 4;   // symbols (m)
    static constexpr float KAISER_ATTEN_DB = 60.0f;
    static constexpr float DB_MIN = -120.0f;   // strip display range
    static constexpr float DB_MAX = 0.0f;

    ChannelizerPanelBlock(const char* name, size_t buffer_size = 32768)
        : BlockBase(name), in(buffer_size)
    {
        // Sized once for the N_MAX x POINTS_MAX worst case (~1.2 MB); never reallocated.
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

    // N = clamp(round(rate/width), N_MIN, N_MAX). Static so the GUI panel can
    // mirror the snapping next to the width control.
    static size_t channels_for(double width_hz, double rate_hz) {
        if (width_hz <= 0.0 || rate_hz <= 0.0) return N_MIN;
        double n = std::round(rate_hz / width_hz);
        n = std::min(std::max(n, static_cast<double>(N_MIN)),
                     static_cast<double>(N_MAX));
        return static_cast<size_t>(n);
    }

    static double effective_width(double width_hz, double rate_hz) {
        if (rate_hz <= 0.0) return 0.0;
        return rate_hz / static_cast<double>(channels_for(width_hz, rate_hz));
    }

    // Stages N, bin size and point spacing as ONE generation, so procedure()
    // never sees torn values. GUI-thread only.
    void set_config(double width_hz, double rate_hz, double span_s,
                    double center_freq_hz) {
        _gui_rate_hz   = rate_hz;
        _gui_center_hz = center_freq_hz;

        // Retired-slot destruction always happens here, on the GUI thread.
        firpfbch_crcf retired = nullptr;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            retired  = _retired;
            _retired = nullptr;
        }
        if (retired) firpfbch_crcf_destroy(retired);

        const size_t N = channels_for(width_hz, rate_hz);
        // One dB point per bin_samples channel samples, chosen so POINTS_MAX
        // points span ~span_s seconds at the channel rate rate/N.
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

        // firpfbch_crcf_create_kaiser() feeds unnormalized taps -> N-dependent
        // +20*log10(N) offset (verified against liquid source); we normalize to unity DC gain instead.
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
        _config_generation.fetch_add(1, std::memory_order_release);
        if (stale_pending) firpfbch_crcf_destroy(stale_pending);
    }

    // Retunes only the Y-tick labels; no DSP reconfiguration. GUI-thread only.
    void set_center_freq(double center_freq_hz) { _gui_center_hz = center_freq_hz; }

    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    void set_visible(bool visible) { _visible = visible; }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    // One-shot programmatic window rect: next render() applies it and clears
    // the request. GUI thread only.
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Analyzer create/destroy always happens on the GUI thread; here we
        // only pointer-swap active/pending/retired under _cfg_mutex.
        const uint64_t gen = _config_generation.load(std::memory_order_acquire);
        if (gen != _config_generation_applied) {
            {
                std::lock_guard<std::mutex> lk(_cfg_mutex);
                if (_pending) {
                    if (_active) _retired = _active;   // GUI drains and destroys this slot
                    _active  = _pending;
                    _pending = nullptr;
                    _N           = _pending_N;
                    _bin_samples = _pending_bin;
                    _point_dt    = _pending_dt;
                }
                _config_generation_applied = _config_generation.load(std::memory_order_relaxed);
            }
            // New channel grid: old history no longer matches, so clear the
            // rings here, on the thread that owns ring writes.
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

        // Not yet configured, or hidden: drain everything, do no DSP work,
        // so the shared fanout can never back up.
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
            // Peak-hold LINEAR power (runs at the full input rate); no
            // log10f here -- that only happens in flush_bin().
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
        if (!_visible) return;
        if (_pending_rect) {
            ImGui::SetNextWindowPos(_pending_rect_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(_pending_rect_size, ImGuiCond_Always);
            _pending_rect = false;
        } else {
            ImGui::SetNextWindowPos(_win_pos, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(_win_size, ImGuiCond_FirstUseEver);
        }
        ImGui::Begin(name());

        // Unroll the rings into chronological order for rendering. On lock
        // contention just redraw last frame's copy.
        if (_snap_mutex.try_lock()) {
            _render_channel_count      = _snap_N;
            _render_fill               = _ring_fill;
            _render_seconds_per_point  = _snap_dt;
            const size_t fill  = _render_fill;
            const size_t start = (_ring_w + POINTS_MAX - fill) % POINTS_MAX;
            const size_t first = std::min(fill, POINTS_MAX - start);
            for (size_t ch = 0; ch < _render_channel_count; ++ch) {
                const float* src = _rings      + ch * POINTS_MAX;
                float*       dst = _render_buf + ch * POINTS_MAX;
                std::memcpy(dst, src + start, first * sizeof(float));
                if (fill > first)
                    std::memcpy(dst + first, src, (fill - first) * sizeof(float));
            }
            _snap_mutex.unlock();
        }

        const size_t N    = _render_channel_count;
        const size_t fill = _render_fill;
        const double span_s = static_cast<double>(POINTS_MAX)
                            * static_cast<double>(_render_seconds_per_point);

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
            // Re-fit Y only when the strip count changes, so the user can
            // otherwise zoom freely into a band of strips.
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
                    _render_x[j] = -static_cast<float>(fill - 1 - j) * _render_seconds_per_point;

                // FFT ordering: f_k = k*rate/N for k<=N/2, else (k-N)*rate/N.
                // Row r (bottom = lowest freq) shows channel (r - N/2) mod N.
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
    // Converts the completed bin's peak power to dB and pushes one point per
    // channel. Runs at the display point rate (a few kpoints/s), so the lock is cheap.
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

    // Y-tick label cache, rebuilt only when N / rate / center change.
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

    // Fixed storage, allocated once in the constructor.
    float*               _rings      = nullptr;  // [N_MAX][POINTS_MAX] dB, guarded by _snap_mutex
    float*               _render_buf = nullptr;  // GUI chronological copy
    float*               _render_x   = nullptr;  // shared strip time axis
    float*               _line_y     = nullptr;  // one strip's offset polyline
    std::complex<float>* _in_scratch = nullptr;  // FRAME_BUDGET analyzer frames
    std::complex<float>* _chan_out   = nullptr;  // one sample per channel
    float*               _accum      = nullptr;  // linear peak power per channel

    // Analyzer slots: created/destroyed on the GUI thread; procedure() only
    // moves pointers under _cfg_mutex (see power_detector.hpp for the same pattern).
    std::mutex    _cfg_mutex;   // guards _active/_pending/_retired and the *_N/_bin/_dt below
    firpfbch_crcf _active  = nullptr;   // owned by the DSP thread's hot path
    firpfbch_crcf _pending = nullptr;   // staged by GUI, not yet consumed
    firpfbch_crcf _retired = nullptr;   // swapped out; awaiting GUI destroy
    size_t        _pending_N   = 0;
    size_t        _pending_bin = 1;
    float         _pending_dt  = 0.0f;

    std::atomic<uint64_t> _config_generation{0};
    uint64_t              _config_generation_applied = 0;
    std::atomic<bool>     _external_pause{false};

    // DSP-thread copies of the applied config.
    size_t _N           = 0;
    size_t _bin_samples = 1;
    float  _point_dt    = 0.0f;
    size_t _bin_count   = 0;

    std::mutex _snap_mutex;   // guards _rings/_ring_w/_ring_fill/_snap_N/_snap_dt
    size_t     _ring_w    = 0;      // next point slot (shared by all channels)
    size_t     _ring_fill = 0;      // valid points (saturates at POINTS_MAX)
    size_t     _snap_N    = 0;      // N the rings correspond to
    float      _snap_dt   = 0.0f;   // seconds per ring point

    // GUI-thread state (setters + render() share the GUI thread).
    size_t _staged_N   = 0;         // last staged grid (skip no-op restages)
    size_t _staged_bin = 0;
    float  _staged_dt  = -1.0f;
    double _gui_rate_hz   = 0.0;    // label-only params
    double _gui_center_hz = 0.0;
    size_t _render_channel_count      = 0;   // last snapshot copied for rendering
    size_t _render_fill               = 0;
    float  _render_seconds_per_point  = 0.0f;
    size_t _y_fit_N = 0;            // strip count the Y axis was last fit for

    size_t _labels_N = 0;           // tick-label cache keys
    double _labels_rate = -1.0, _labels_center = -1.0;
    std::vector<double>      _tick_pos;
    std::vector<std::string> _tick_labels;
    std::vector<const char*> _tick_ptrs;

    bool   _visible = true;
    ImVec2 _win_pos{380.0f, 455.0f};
    ImVec2 _win_size{1100.0f, 430.0f};
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos{0.0f, 0.0f};
    ImVec2 _pending_rect_size{0.0f, 0.0f};
};
