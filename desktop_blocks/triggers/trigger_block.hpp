#pragma once

#include "cler.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>

// Zero-span style capture trigger for a real-valued (e.g. dB power) stream,
// rendered as an OSCILLOSCOPE: every trigger paints one fixed-width window with
// the trigger event pinned at t=0 (pre-trigger to the left), replacing the
// previous frame -- not a scrolling display.
//
// Design notes (why it looks the way it does):
//  * The block is a SINK: it consumes its input continuously (so it never
//    backs up the USRP) and renders the captured window itself. There is no
//    downstream channel, so there is no output-backpressure hazard.
//  * STATE persists across procedure() calls (the scheduler keeps no per-call
//    state). Edge detection uses a hysteresis latch that survives call
//    boundaries, so triggers are not missed or duplicated at the seams.
//  * The completed window is published to a snapshot buffer under a mutex; the
//    GUI thread reads it in render(). The block thread never touches ImGui.
//  * LIVE RECONFIG goes through a mutex-guarded "pending" config that the block
//    thread copies into its active config only at a safe point (ARMED/IDLE).
//    Buffers are sized once for the maximum window at construction -- no
//    allocation, push/pop, or blocking in procedure() (the hot path).
template <typename T = float>
struct TriggerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    enum class Edge   { Rising, Falling };
    enum class Mode   { Normal, Single, Auto };
    enum class State  { Idle, Armed, Capturing };

    TriggerBlock(const char* name,
                 size_t sample_rate,
                 float  threshold,          // trigger level (same units as input, e.g. dB)
                 float  window_ms,          // total capture window length
                 float  pretrigger_pct = 10.0f,   // % of window shown before the trigger
                 float  holdoff_ms     = 100.0f,  // min time between triggers
                 Edge   edge           = Edge::Rising,
                 Mode   mode           = Mode::Auto,
                 float  hysteresis     = 3.0f,    // re-arm band below/above level
                 float  auto_ms        = 200.0f,  // free-run timeout in Auto mode
                 float  max_window_ms  = 0.0f,    // 0 => use window_ms as the max
                 size_t buffer_size    = 65536)
        : BlockBase(name),
          in(buffer_size),
          _sample_rate(sample_rate)
    {
        float max_ms = (max_window_ms > 0.0f) ? std::max(max_window_ms, window_ms) : window_ms;
        _max_window  = ms_to_samples(max_ms);
        if (_max_window < 1) _max_window = 1;
        // Hard ceiling so a long window at a high sample rate can't blow up memory
        // (~16 bytes/sample across the full-res buffers). 16 Msamples ~= 256 MB.
        if (_max_window > MAX_CAPTURE_SAMPLES) _max_window = MAX_CAPTURE_SAMPLES;

        _capture  = new T[_max_window];
        _ring     = new T[_max_window];   // pre-trigger ring is at most the whole window
        _snap_buf = new float[_max_window];   // published frame (DSP thread, under lock)
        _render_y = new float[_max_window];   // GUI's private copy (decimated at draw time)
        // Display arrays are bounded by screen resolution, not window length.
        _x_render = new float[MAX_PLOT_POINTS];
        _plot_x   = new float[MAX_PLOT_POINTS];
        _plot_y   = new float[MAX_PLOT_POINTS];

        Config c;
        c.threshold          = threshold;
        c.window_samples     = clamp_window(ms_to_samples(window_ms));
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.holdoff_samples    = ms_to_samples(holdoff_ms);
        c.auto_samples       = ms_to_samples(auto_ms);
        c.edge               = edge;
        c.mode               = mode;
        c.hysteresis         = std::max(0.0f, hysteresis);
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            _pending     = c;
            _pending_gen = 1;
        }
        _active = c;
        _state  = State::Armed;
    }

    ~TriggerBlock() {
        delete[] _capture;
        delete[] _ring;
        delete[] _snap_buf;
        delete[] _render_y;
        delete[] _x_render;
        delete[] _plot_x;
        delete[] _plot_y;
    }

    // ---- live control surface (called from the GUI/render thread) ----
    void set_config(float threshold, float window_ms, float pretrigger_pct,
                    float holdoff_ms, Edge edge, Mode mode,
                    float hysteresis, float auto_ms) {
        Config c;
        c.threshold          = threshold;
        c.window_samples     = clamp_window(ms_to_samples(window_ms));
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.holdoff_samples    = ms_to_samples(holdoff_ms);
        c.auto_samples       = ms_to_samples(auto_ms);
        c.edge               = edge;
        c.mode               = mode;
        c.hysteresis         = std::max(0.0f, hysteresis);
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending = c;
        ++_pending_gen;
    }

    void force_trigger() { _force.store(true, std::memory_order_release); }
    void rearm()         { _rearm.store(true, std::memory_order_release); }

    State  state()       const { return _state.load(std::memory_order_acquire); }
    size_t sample_rate() const { return _sample_rate; }
    size_t max_window_samples() const { return _max_window; }
    float  max_window_ms() const {
        return 1000.0f * static_cast<float>(_max_window) / static_cast<float>(_sample_rate);
    }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        State st = _state.load(std::memory_order_relaxed);
        if (st == State::Armed || st == State::Idle) {
            maybe_apply_config();
            if (_rearm.exchange(false, std::memory_order_acq_rel)) { arm(); st = State::Armed; }
        }
        switch (st) {
            case State::Idle:      return drain_input_idle();
            case State::Armed:     return scan_for_trigger();
            case State::Capturing: return fill_capture();
        }
        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_win_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_win_pos, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());

        // Pull the latest frame (cheap copy under lock; never blocks the DSP thread long).
        size_t len = 0, trig_idx = 0;
        float  pre_ms = 0.0f, post_ms = 0.0f, level = 0.0f;
        unsigned long frame = 0;
        if (_snap_mutex.try_lock()) {
            len      = _snap_len;
            trig_idx = _snap_trig_idx;
            pre_ms   = _snap_pre_ms;
            post_ms  = _snap_post_ms;
            level    = _snap_level;
            frame    = _frame_count;
            if (len > 0) std::memcpy(_render_y, _snap_buf, len * sizeof(float));
            _snap_mutex.unlock();
        } else {
            // Couldn't grab the lock this frame; reuse last render copy.
            len      = _render_len;
            trig_idx = _render_trig;
            pre_ms   = _render_pre_ms;
            post_ms  = _render_post_ms;
            level    = _render_level;
            frame    = _render_frame;
        }
        _render_len = len; _render_trig = trig_idx; _render_pre_ms = pre_ms;
        _render_post_ms = post_ms; _render_level = level; _render_frame = frame;

        ImGui::Text("Frames: %lu", frame);
        ImGui::SameLine();
        ImGui::Text("| trigger @ t=0, window [%.1f, %.1f] ms", -pre_ms, post_ms);

        if (len == 0) {
            ImGui::TextUnformatted("Waiting for trigger...");
            ImGui::End();
            return;
        }

        // Build the trigger-relative time axis (ms). Trigger sample sits at t=0.
        // For windows larger than the screen can resolve, draw a min/max envelope
        // so the plot stays smooth AND transient peaks are preserved.
        const float dt_ms = 1000.0f / static_cast<float>(_sample_rate);
        const float* plot_x;
        const float* plot_y;
        int          plot_n;
        if (len <= MAX_PLOT_POINTS) {
            for (size_t i = 0; i < len; ++i)
                _x_render[i] = (static_cast<float>(i) - static_cast<float>(trig_idx)) * dt_ms;
            plot_x = _x_render; plot_y = _render_y; plot_n = static_cast<int>(len);
        } else {
            size_t nb = MAX_PLOT_POINTS / 2;             // 2 points (min,max) per bucket
            for (size_t b = 0; b < nb; ++b) {
                size_t s = (b * len) / nb;
                size_t e = ((b + 1) * len) / nb;
                if (e <= s) e = s + 1;
                if (e > len) e = len;
                float mn = _render_y[s], mx = _render_y[s];
                for (size_t i = s + 1; i < e; ++i) {
                    mn = std::min(mn, _render_y[i]);
                    mx = std::max(mx, _render_y[i]);
                }
                _plot_x[2 * b]     = (static_cast<float>(s)     - static_cast<float>(trig_idx)) * dt_ms;
                _plot_y[2 * b]     = mn;
                _plot_x[2 * b + 1] = (static_cast<float>(e - 1) - static_cast<float>(trig_idx)) * dt_ms;
                _plot_y[2 * b + 1] = mx;
            }
            plot_x = _plot_x; plot_y = _plot_y; plot_n = static_cast<int>(2 * nb);
        }

        if (ImPlot::BeginPlot("##scope", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time [ms]", "Power [dB]");
            // Fixed timebase so the trigger point stays put across frames.
            ImPlot::SetupAxisLimits(ImAxis_X1, -pre_ms, post_ms, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -120.0, 10.0, ImPlotCond_Once);

            ImPlot::PlotLine("Power", plot_x, plot_y, plot_n);

            // Trigger level (horizontal) and trigger instant (vertical).
            double lvl = level;
            ImPlotSpec hspec;
            hspec.Flags = ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("Level", &lvl, 1, hspec);
            double t0 = 0.0;
            ImPlot::PlotInfLines("Trig", &t0, 1);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    struct Config {
        float  threshold          = 0.0f;
        size_t window_samples     = 1;
        size_t pretrigger_samples = 0;
        size_t holdoff_samples    = 0;
        size_t auto_samples       = 0;
        Edge   edge               = Edge::Rising;
        Mode   mode               = Mode::Auto;
        float  hysteresis         = 0.0f;
    };

    size_t ms_to_samples(float ms) const {
        long n = static_cast<long>((ms / 1000.0f) * static_cast<float>(_sample_rate));
        return n < 0 ? 0 : static_cast<size_t>(n);
    }
    size_t clamp_window(size_t w) const {
        if (w < 1) w = 1;
        return std::min(w, _max_window);
    }
    static size_t clamp_pre(size_t p, size_t window) {
        if (window == 0) return 0;
        return std::min(p, window - 1);   // leave >=1 post-trigger sample
    }

    void maybe_apply_config() {
        size_t gen = _pending_gen.load(std::memory_order_acquire);
        if (gen == _applied_gen) return;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            _active      = _pending;
            _applied_gen = gen;
        }
        _active.window_samples     = clamp_window(_active.window_samples);
        _active.pretrigger_samples = clamp_pre(_active.pretrigger_samples, _active.window_samples);
        reset_ring();
        reset_edge_latch();
        _holdoff_counter = 0;
        _auto_counter    = 0;
    }

    void arm() {
        _state.store(State::Armed, std::memory_order_release);
        _auto_counter    = 0;
        _holdoff_counter = 0;
    }
    void reset_ring()       { _ring_w = 0; _ring_fill = 0; }
    void reset_edge_latch() { _armed_band = false; }

    inline void ring_push(T s) {
        size_t pre = _active.pretrigger_samples;
        if (pre == 0) return;
        _ring[_ring_w] = s;
        _ring_w = (_ring_w + 1) % pre;
        if (_ring_fill < pre) _ring_fill++;
    }

    // Hysteresis-latched edge detector; the latch persists across procedure() calls.
    inline bool edge_fires(T s) {
        const float lvl = _active.threshold;
        const float h   = _active.hysteresis;
        if (_active.edge == Edge::Rising) {
            if (s < lvl - h) _armed_band = true;
            if (_armed_band && s >= lvl) { _armed_band = false; return true; }
        } else {
            if (s > lvl + h) _armed_band = true;
            if (_armed_band && s <= lvl) { _armed_band = false; return true; }
        }
        return false;
    }

    cler::Result<cler::Empty, cler::Error> drain_input_idle() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        const T* p1; const T* p2; size_t s1, s2;
        in.peek_read(p1, s1, p2, s2);
        size_t n = std::min(avail, s1 + s2);
        for (size_t i = 0; i < n; ++i) {
            T s = (i < s1) ? p1[i] : p2[i - s1];
            ring_push(s);
            (void)edge_fires(s);
        }
        in.commit_read(n);
        return cler::Empty{};
    }

    cler::Result<cler::Empty, cler::Error> scan_for_trigger() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        const bool forced = _force.exchange(false, std::memory_order_acq_rel);

        const T* p1; const T* p2; size_t s1, s2;
        in.peek_read(p1, s1, p2, s2);
        size_t n = std::min(avail, s1 + s2);

        size_t consumed = 0;
        bool   triggered = false;
        bool   force_now = forced;
        for (size_t i = 0; i < n; ++i) {
            T s = (i < s1) ? p1[i] : p2[i - s1];
            ring_push(s);

            bool can_trigger = (_holdoff_counter == 0);
            if (_holdoff_counter > 0) _holdoff_counter--;

            bool fire = false;
            if (can_trigger) {
                if (force_now) fire = true;
                else if (edge_fires(s)) fire = true;
                else if (_active.mode == Mode::Auto && _active.auto_samples > 0 &&
                         ++_auto_counter >= _active.auto_samples) fire = true;
            } else {
                (void)edge_fires(s);
            }
            force_now = false;
            consumed++;
            if (fire) { triggered = true; break; }
        }
        in.commit_read(consumed);
        if (triggered) begin_capture();
        return cler::Empty{};
    }

    void begin_capture() {
        size_t pre      = _active.pretrigger_samples;
        size_t pre_have = std::min(_ring_fill, pre);
        if (pre > 0 && pre_have > 0) {
            size_t start = (_ring_w + pre - pre_have) % pre;   // oldest sample
            for (size_t j = 0; j < pre_have; ++j)
                _capture[j] = _ring[(start + j) % pre];
        }
        size_t post   = _active.window_samples - _active.pretrigger_samples;
        _capture_fill = pre_have;
        _capture_len  = std::min(pre_have + post, _max_window);
        _capture_trig = pre_have;          // trigger sits right after the pre-trigger run
        _auto_counter = 0;
        if (_capture_fill >= _capture_len) publish_frame();
        else _state.store(State::Capturing, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> fill_capture() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        size_t need = _capture_len - _capture_fill;
        size_t got  = in.readN(_capture + _capture_fill, std::min(avail, need));
        _capture_fill += got;
        if (_capture_fill >= _capture_len) publish_frame();
        return cler::Empty{};
    }

    void publish_frame() {
        const float dt_ms = 1000.0f / static_cast<float>(_sample_rate);
        {
            std::lock_guard<std::mutex> lk(_snap_mutex);
            std::memcpy(_snap_buf, _capture, _capture_len * sizeof(T));
            _snap_len      = _capture_len;
            _snap_trig_idx = _capture_trig;
            _snap_pre_ms   = static_cast<float>(_capture_trig) * dt_ms;
            _snap_post_ms  = static_cast<float>(_capture_len - _capture_trig) * dt_ms;
            _snap_level    = _active.threshold;
            ++_frame_count;
        }
        _holdoff_counter = _active.holdoff_samples;
        reset_edge_latch();
        if (_active.mode == Mode::Single) _state.store(State::Idle, std::memory_order_release);
        else arm();
    }

    // ---- config / threading ----
    std::mutex            _cfg_mutex;
    Config                _pending;
    Config                _active;
    std::atomic<size_t>   _pending_gen{0};
    size_t                _applied_gen{0};
    std::atomic<bool>     _force{false};
    std::atomic<bool>     _rearm{false};
    std::atomic<State>    _state{State::Armed};

    // ---- fixed params / buffers ----
    size_t  _sample_rate;
    size_t  _max_window = 0;
    T*      _capture = nullptr;
    T*      _ring    = nullptr;

    // ---- snapshot shared with GUI (guarded by _snap_mutex) ----
    std::mutex    _snap_mutex;
    float*        _snap_buf = nullptr;          // published frame, written by DSP thread under lock
    size_t        _snap_len = 0;
    size_t        _snap_trig_idx = 0;
    float         _snap_pre_ms = 0.0f;
    float         _snap_post_ms = 0.0f;
    float         _snap_level = 0.0f;
    unsigned long _frame_count = 0;

    // Display is bounded by screen resolution; large windows are decimated to
    // a min/max envelope of at most this many points.
    static constexpr size_t MAX_PLOT_POINTS    = 8000;
    static constexpr size_t MAX_CAPTURE_SAMPLES = 16u * 1024 * 1024;  // ~256 MB across buffers

    // ---- GUI-thread private render copies ----
    float*        _render_y = nullptr;          // copy of last frame to plot from
    float*        _x_render = nullptr;          // time axis (small / direct path)
    float*        _plot_x = nullptr;            // decimated envelope x (large windows)
    float*        _plot_y = nullptr;            // decimated envelope y
    size_t        _render_len = 0, _render_trig = 0;
    float         _render_pre_ms = 0.0f, _render_post_ms = 0.0f, _render_level = 0.0f;
    unsigned long _render_frame = 0;

    // ---- runtime state (block thread only) ----
    size_t  _ring_w = 0, _ring_fill = 0;
    size_t  _capture_fill = 0, _capture_len = 0, _capture_trig = 0;
    size_t  _holdoff_counter = 0, _auto_counter = 0;
    bool    _armed_band = false;

    ImVec2 _win_pos{380.0f, 10.0f};
    ImVec2 _win_size{1100.0f, 430.0f};
};
