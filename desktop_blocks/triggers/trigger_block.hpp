#pragma once

#include "cler.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <vector>

// Zero-span capture trigger for a real-valued stream, rendered as an
// oscilloscope: each trigger paints one fixed window pinned at t=0, replacing
// the previous frame. Sink block (no output channel): consumes continuously
// so it never backs up the upstream source. procedure() (block thread) never
// touches ImGui; it publishes to a mutex-guarded snapshot that render() (GUI
// thread) reads.
template <typename T = float>
struct TriggerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    enum class Edge   { Rising, Falling };
    enum class Mode   { Normal, Single, Auto };
    enum class State  { Idle, Armed, Capturing };

    TriggerBlock(const char* name,
                 size_t sample_rate,
                 float  threshold,          // same units as the input stream, e.g. dB
                 float  window_ms,
                 float  pretrigger_pct = 10.0f,
                 float  holdoff_ms     = 100.0f,
                 Edge   edge           = Edge::Rising,
                 Mode   mode           = Mode::Auto,
                 float  hysteresis     = 3.0f,
                 float  auto_ms        = 200.0f,
                 float  max_window_ms  = 0.0f,    // 0 => use window_ms as the max
                 size_t buffer_size    = 65536)
        : BlockBase(name),
          in(buffer_size),
          _ctor_rate(sample_rate)
    {
        float max_ms = (max_window_ms > 0.0f) ? std::max(max_window_ms, window_ms) : window_ms;
        _max_window  = ms_to_samples(max_ms);
        if (_max_window < 1) _max_window = 1;
        // Ceiling: ~16 bytes/sample across buffers, so 16 Msamples ~= 256 MB.
        if (_max_window > MAX_CAPTURE_SAMPLES) _max_window = MAX_CAPTURE_SAMPLES;

        _capture  = new T[_max_window];
        _ring     = new T[_max_window];
        _snap_buf = new float[_max_window];   // published frame; DSP thread writes it under _snap_mutex
        _render_y = new float[_max_window];
        _x_render = new float[MAX_PLOT_POINTS];
        _plot_x   = new float[MAX_PLOT_POINTS];
        _plot_y   = new float[MAX_PLOT_POINTS];

        _requested_rate.store(_ctor_rate, std::memory_order_release);
        _snap_rate = _ctor_rate;
        _render_rate = _ctor_rate;

        Config c;
        c.sample_rate        = _ctor_rate;
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

    // GUI/render thread. sample_rate is part of Config so a rate change and its
    // derived sample counts land on the block thread as one generation. Capture
    // buffers are sized once at construction and never reallocate, so the
    // achievable window in milliseconds shrinks as the live rate rises.
    void set_config(float threshold, float window_ms, float pretrigger_pct,
                    float holdoff_ms, Edge edge, Mode mode,
                    float hysteresis, float auto_ms, size_t sample_rate) {
        if (sample_rate < 1) sample_rate = 1;
        Config c;
        c.sample_rate        = sample_rate;
        c.threshold          = threshold;
        c.window_samples     = clamp_window(ms_to_samples_at(window_ms, sample_rate));
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.holdoff_samples    = ms_to_samples_at(holdoff_ms, sample_rate);
        c.auto_samples       = ms_to_samples_at(auto_ms, sample_rate);
        c.edge               = edge;
        c.mode               = mode;
        c.hysteresis         = std::max(0.0f, hysteresis);
        _requested_rate.store(sample_rate, std::memory_order_release);
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending = c;
        ++_pending_gen;
    }

    void force_trigger() { _force.store(true, std::memory_order_release); }
    void rearm()         { _rearm.store(true, std::memory_order_release); }

    State  state()       const { return _state.load(std::memory_order_acquire); }
    // Published-frame counter, lock-free mirror of the snapshot's _frame_count
    // (same value render() shows as "Frames"). Lets a poller notice a new
    // capture without export_frame()'s copy or touching _snap_mutex.
    unsigned long frame_count() const {
        return _frames_published.load(std::memory_order_acquire);
    }
    // Rate of the most recently requested config (ctor rate until set_config
    // is first called). max_window_ms() is the fixed sample capacity expressed
    // at that rate, so it shrinks as the requested rate rises.
    size_t sample_rate() const { return _requested_rate.load(std::memory_order_acquire); }
    size_t max_window_samples() const { return _max_window; }
    float  max_window_ms() const {
        return 1000.0f * static_cast<float>(_max_window)
             / static_cast<float>(_requested_rate.load(std::memory_order_acquire));
    }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    // Copies the latest published frame (same one render() draws) into caller
    // storage; t_ms[i] = (i - trig_idx) * 1000 / sample_rate_hz, trigger at t=0.
    // sample_rate_hz is the rate the frame was captured at, not the live rate.
    // May allocate; call from the GUI thread only. False if nothing published yet.
    bool export_frame(std::vector<float>& samples, float& pre_ms, float& post_ms,
                      size_t& trig_idx, unsigned long& frame_count,
                      size_t& sample_rate_hz) {
        std::lock_guard<std::mutex> lk(_snap_mutex);
        if (_snap_len == 0) return false;
        samples.assign(_snap_buf, _snap_buf + _snap_len);
        pre_ms         = _snap_pre_ms;
        post_ms        = _snap_post_ms;
        trig_idx       = _snap_trig_idx;
        frame_count    = _frame_count;
        sample_rate_hz = _snap_rate;
        return true;
    }

    // One-shot: the next render() applies this rect then clears the request, so
    // the user can move/resize afterward. GUI thread only (procedure() never touches these).
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        State st = _state.load(std::memory_order_relaxed);
        // Abort an in-flight capture on reconfig/re-arm: it was sized in samples
        // under the OLD rate, so at a much lower new rate it could take tens of
        // seconds to fill and freeze the scope until then. Discard (never
        // published) and fall through so the new config applies immediately.
        if (st == State::Capturing &&
            (_pending_gen.load(std::memory_order_acquire) != _applied_gen ||
             _rearm.load(std::memory_order_acquire))) {
            _state.store(State::Armed, std::memory_order_release);
            st = State::Armed;
        }
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
        // Always is applied once, not every frame, or the window would snap
        // back and become unresizable.
        if (_pending_rect) {
            ImGui::SetNextWindowPos(_pending_rect_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(_pending_rect_size, ImGuiCond_Always);
            _pending_rect = false;
        } else {
            ImGui::SetNextWindowSize(_win_size, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(_win_pos, ImGuiCond_FirstUseEver);
        }
        ImGui::Begin(name());

        size_t len = 0, trig_idx = 0, rate = _ctor_rate;
        float  pre_ms = 0.0f, post_ms = 0.0f, level = 0.0f;
        unsigned long frame = 0;
        if (_snap_mutex.try_lock()) {
            len      = _snap_len;
            trig_idx = _snap_trig_idx;
            pre_ms   = _snap_pre_ms;
            post_ms  = _snap_post_ms;
            level    = _snap_level;
            frame    = _frame_count;
            rate     = _snap_rate;
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
            rate     = _render_rate;
        }
        _render_len = len; _render_trig = trig_idx; _render_pre_ms = pre_ms;
        _render_post_ms = post_ms; _render_level = level; _render_frame = frame;
        _render_rate = rate;

        ImGui::Text("Frames: %lu", frame);
        ImGui::SameLine();
        ImGui::Text("| trigger @ t=0, window [%.1f, %.1f] ms", -pre_ms, post_ms);

        if (len == 0) {
            ImGui::TextUnformatted("Waiting for trigger...");
            ImGui::End();
            return;
        }

        // rate is the snapshot's captured rate, so a live rate change never
        // mislabels an already-published frame.
        const float dt_ms = 1000.0f / static_cast<float>(rate);
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
        size_t sample_rate        = 1;   // rate the sample counts below were derived at
        float  threshold          = 0.0f;
        size_t window_samples     = 1;
        size_t pretrigger_samples = 0;
        size_t holdoff_samples    = 0;
        size_t auto_samples       = 0;
        Edge   edge               = Edge::Rising;
        Mode   mode               = Mode::Auto;
        float  hysteresis         = 0.0f;
    };

    static size_t ms_to_samples_at(float ms, size_t rate) {
        long n = static_cast<long>((ms / 1000.0f) * static_cast<float>(rate));
        return n < 0 ? 0 : static_cast<size_t>(n);
    }
    size_t ms_to_samples(float ms) const { return ms_to_samples_at(ms, _ctor_rate); }
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

    // Latch persists across procedure() calls, so an edge is never missed or double-fired at a call seam.
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
        // Keeps the pre-trigger ring advancing during capture too, otherwise
        // consecutive frames' pre-trigger segments visibly shift at the seam.
        for (size_t i = 0; i < got; ++i)
            ring_push(_capture[_capture_fill + i]);
        _capture_fill += got;
        if (_capture_fill >= _capture_len) publish_frame();
        return cler::Empty{};
    }

    void publish_frame() {
        // Uses _active.sample_rate (the rate this frame was captured at), not
        // the ctor rate, since the rate can change live.
        const float dt_ms = 1000.0f / static_cast<float>(_active.sample_rate);
        {
            std::lock_guard<std::mutex> lk(_snap_mutex);
            std::memcpy(_snap_buf, _capture, _capture_len * sizeof(T));
            _snap_len      = _capture_len;
            _snap_trig_idx = _capture_trig;
            _snap_rate     = _active.sample_rate;
            // From the configured window, not captured length, so the trigger
            // line stays fixed at the pre-trigger fraction across frames.
            _snap_pre_ms   = static_cast<float>(_active.pretrigger_samples) * dt_ms;
            _snap_post_ms  = static_cast<float>(_active.window_samples
                                                - _active.pretrigger_samples) * dt_ms;
            _snap_level    = _active.threshold;
            ++_frame_count;
        }
        // Published after the lock so a poller that sees the bump can then read
        // a fully written snapshot.
        _frames_published.store(_frame_count, std::memory_order_release);
        _holdoff_counter = _active.holdoff_samples;
        reset_edge_latch();
        if (_active.mode == Mode::Single) _state.store(State::Idle, std::memory_order_release);
        else arm();
    }

    std::mutex            _cfg_mutex;
    Config                _pending;
    Config                _active;
    std::atomic<size_t>   _pending_gen{0};
    size_t                _applied_gen{0};
    std::atomic<bool>     _force{false};
    std::atomic<bool>     _rearm{false};
    std::atomic<State>    _state{State::Armed};

    // _ctor_rate sizes buffer capacity and seeds the initial config; the live
    // rate travels inside Config, _requested_rate mirrors it for accessors.
    size_t  _ctor_rate;
    std::atomic<size_t> _requested_rate{1};
    size_t  _max_window = 0;
    T*      _capture = nullptr;
    T*      _ring    = nullptr;

    // Snapshot shared with the GUI thread; guarded by _snap_mutex.
    std::mutex    _snap_mutex;
    float*        _snap_buf = nullptr;          // written by the block thread under lock
    size_t        _snap_len = 0;
    size_t        _snap_trig_idx = 0;
    float         _snap_pre_ms = 0.0f;
    float         _snap_post_ms = 0.0f;
    float         _snap_level = 0.0f;
    size_t        _snap_rate = 1;   // rate the published frame was captured at
    unsigned long _frame_count = 0;
    // Lock-free mirror of _frame_count for frame_count(); written by the block
    // thread just after publish_frame() releases _snap_mutex.
    std::atomic<unsigned long> _frames_published{0};

    static constexpr size_t MAX_PLOT_POINTS    = 8000;  // large windows decimate to a min/max envelope of this many points
    static constexpr size_t MAX_CAPTURE_SAMPLES = 16u * 1024 * 1024;  // ~256 MB across buffers

    // GUI-thread-only render copies.
    float*        _render_y = nullptr;
    float*        _x_render = nullptr;
    float*        _plot_x = nullptr;   // decimated envelope x (large windows)
    float*        _plot_y = nullptr;   // decimated envelope y
    size_t        _render_len = 0, _render_trig = 0;
    size_t        _render_rate = 1;
    float         _render_pre_ms = 0.0f, _render_post_ms = 0.0f, _render_level = 0.0f;
    unsigned long _render_frame = 0;

    // Block-thread-only runtime state.
    size_t  _ring_w = 0, _ring_fill = 0;
    size_t  _capture_fill = 0, _capture_len = 0, _capture_trig = 0;
    size_t  _holdoff_counter = 0, _auto_counter = 0;
    bool    _armed_band = false;

    ImVec2 _win_pos{380.0f, 10.0f};
    ImVec2 _win_size{1100.0f, 430.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos{0.0f, 0.0f};
    ImVec2 _pending_rect_size{0.0f, 0.0f};
};
