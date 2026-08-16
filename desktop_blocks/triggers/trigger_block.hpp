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
//
// Storage is a fixed budget of POINTS, not of samples. A window that fits the
// budget is stored raw (stride 1, byte-identical to a plain capture). A longer
// one is stored as a min/max envelope: one (min,max) pair per `stride` input
// samples, interleaved, so a frame never exceeds the budget no matter the rate.
// Detection always runs on every input sample -- only storage is reduced -- so
// a burst far shorter than one bucket still fires the trigger.
template <typename T = float>
struct TriggerBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
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
        _max_window_ms = (max_window_ms > 0.0f) ? std::max(max_window_ms, window_ms) : window_ms;
        if (_max_window_ms < 0.0f) _max_window_ms = 0.0f;

        // Point budget: enough to hold the longest window raw at the ctor rate,
        // floored so the display is never starved and capped so a high ctor rate
        // cannot demand gigabytes. Beyond it, stride_for() trades resolution.
        size_t want = ms_to_samples(_max_window_ms);
        if (want < MIN_CAPTURE_POINTS) want = MIN_CAPTURE_POINTS;
        _capacity = std::min(want, MAX_CAPTURE_POINTS) & ~size_t{1};   // even: envelope points are pairs

        _capture  = new T[_capacity];
        _ring     = new T[_capacity];
        _snap_buf = new float[_capacity];   // published frame; DSP thread writes it under _snap_mutex
        _render_y = new float[_capacity];
        _x_render = new float[MAX_PLOT_POINTS];
        _plot_x   = new float[MAX_PLOT_POINTS];
        _plot_y   = new float[MAX_PLOT_POINTS];

        _requested_rate.store(_ctor_rate, std::memory_order_release);
        _snap_rate = _ctor_rate;
        _render_rate = _ctor_rate;

        Config c;
        c.sample_rate        = _ctor_rate;
        c.threshold          = threshold;
        c.window_samples     = clamp_window_at(ms_to_samples(window_ms), _ctor_rate);
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.stride             = stride_for(c.window_samples);
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
    // buffers are sized once at construction and never reallocate; a rate rise
    // costs resolution (a coarser stride), not window length.
    void set_config(float threshold, float window_ms, float pretrigger_pct,
                    float holdoff_ms, Edge edge, Mode mode,
                    float hysteresis, float auto_ms, size_t sample_rate) {
        if (sample_rate < 1) sample_rate = 1;
        Config c;
        c.sample_rate        = sample_rate;
        c.threshold          = threshold;
        c.window_samples     = clamp_window_at(ms_to_samples_at(window_ms, sample_rate), sample_rate);
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.stride             = stride_for(c.window_samples);
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
    // is first called).
    size_t sample_rate() const { return _requested_rate.load(std::memory_order_acquire); }

    // Longest reachable window. Fixed at construction and independent of the
    // live rate: past the point budget the frame is stored as an envelope
    // instead of being truncated.
    float  max_window_ms() const { return _max_window_ms; }
    size_t capture_points() const { return _capacity; }
    // Input samples per stored (min,max) pair at this window/rate; 1 = raw.
    // Lets a GUI show the resolution a long window is actually captured at.
    size_t stride_for_window_ms(float ms, size_t rate) const {
        if (rate < 1) rate = 1;
        return stride_for(clamp_window_at(ms_to_samples_at(ms, rate), rate));
    }

    // Input-sample offset of stored point i within a frame captured at `stride`.
    // Envelope points are interleaved (min,max) per bucket: the min is placed at
    // the bucket's first input sample, the max at its last, so the two never
    // share an x and the trace keeps its span.
    static size_t frame_input_index(size_t i, size_t stride) {
        if (stride <= 1) return i;
        const size_t bucket = i >> 1;
        return (i & 1u) ? bucket * stride + stride - 1 : bucket * stride;
    }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    // Copies the latest published frame (same one render() draws) into caller
    // storage. trig_idx and frame_input_index() are both in INPUT SAMPLES, so
    //   t_ms[i] = (frame_input_index(i, stride) - trig_idx) * 1000 / sample_rate_hz
    // puts the trigger at t=0 for a raw frame (stride 1) and an envelope alike.
    // sample_rate_hz is the rate the frame was captured at, not the live rate.
    // May allocate; call from the GUI thread only. False if nothing published yet.
    bool export_frame(std::vector<float>& samples, float& pre_ms, float& post_ms,
                      size_t& trig_idx, unsigned long& frame_count,
                      size_t& sample_rate_hz, size_t& stride) {
        std::lock_guard<std::mutex> lk(_snap_mutex);
        if (_snap_len == 0) return false;
        samples.assign(_snap_buf, _snap_buf + _snap_len);
        pre_ms         = _snap_pre_ms;
        post_ms        = _snap_post_ms;
        trig_idx       = _snap_trig_idx;
        frame_count    = _frame_count;
        sample_rate_hz = _snap_rate;
        stride         = _snap_stride;
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

    void set_visible(bool visible) { _visible = visible; }

    void render() {
        if (!_visible) return;
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

        size_t len = 0, trig_idx = 0, rate = _ctor_rate, stride = 1;
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
            stride   = _snap_stride;
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
            stride   = _render_stride;
        }
        _render_len = len; _render_trig = trig_idx; _render_pre_ms = pre_ms;
        _render_post_ms = post_ms; _render_level = level; _render_frame = frame;
        _render_rate = rate; _render_stride = stride;

        ImGui::Text("Frames: %lu", frame);
        ImGui::SameLine();
        ImGui::Text("| trigger @ t=0, window [%.1f, %.1f] ms", -pre_ms, post_ms);
        if (stride > 1) {
            ImGui::SameLine();
            ImGui::TextDisabled("| envelope %.3g us/pt",
                                1e6 * static_cast<double>(stride) / static_cast<double>(rate));
        }

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
        // x always goes through frame_input_index(), so a stored envelope and a
        // raw frame land on the same time axis.
        if (len <= MAX_PLOT_POINTS) {
            for (size_t i = 0; i < len; ++i)
                _x_render[i] = (static_cast<float>(frame_input_index(i, stride))
                                - static_cast<float>(trig_idx)) * dt_ms;
            plot_x = _x_render; plot_y = _render_y; plot_n = static_cast<int>(len);
        } else {
            // Re-reducing an envelope is safe: min over a run of stored points
            // spans that run's mins, max spans its maxes.
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
                _plot_x[2 * b]     = (static_cast<float>(frame_input_index(s, stride))
                                      - static_cast<float>(trig_idx)) * dt_ms;
                _plot_y[2 * b]     = mn;
                _plot_x[2 * b + 1] = (static_cast<float>(frame_input_index(e - 1, stride))
                                      - static_cast<float>(trig_idx)) * dt_ms;
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
    bool _visible = true;

    struct Config {
        size_t sample_rate        = 1;   // rate the sample counts below were derived at
        float  threshold          = 0.0f;
        size_t window_samples     = 1;
        size_t pretrigger_samples = 0;
        size_t stride             = 1;   // input samples per stored (min,max) pair; 1 = raw
        size_t holdoff_samples    = 0;
        size_t auto_samples       = 0;
        Edge   edge               = Edge::Rising;
        Mode   mode               = Mode::Auto;
        float  hysteresis         = 0.0f;
    };

    static size_t ms_to_samples_at(float ms, size_t rate) {
        double n = (static_cast<double>(ms) / 1000.0) * static_cast<double>(rate);
        return n < 0.0 ? 0 : static_cast<size_t>(n);
    }
    size_t ms_to_samples(float ms) const { return ms_to_samples_at(ms, _ctor_rate); }

    // The window is bounded in TIME, not by the point budget -- a window past
    // the budget is stored coarser rather than cut short.
    size_t clamp_window_at(size_t w, size_t rate) const {
        if (w < 1) w = 1;
        return std::min(w, std::max<size_t>(1, ms_to_samples_at(_max_window_ms, rate)));
    }
    static size_t clamp_pre(size_t p, size_t window) {
        if (window == 0) return 0;
        return std::min(p, window - 1);   // leave >=1 post-trigger sample
    }

    // Raw while the window fits the budget; past it, the coarsest stride that
    // brings 2*ceil(window/stride) points back under it.
    size_t stride_for(size_t window) const {
        if (window <= _capacity) return 1;
        const size_t buckets = _capacity / 2;
        return (window + buckets - 1) / buckets;
    }
    // Stored points a fully captured `window` occupies at `stride`.
    static size_t points_for(size_t window, size_t stride) {
        if (stride <= 1) return window;
        return 2 * ((window + stride - 1) / stride);
    }

    void maybe_apply_config() {
        size_t gen = _pending_gen.load(std::memory_order_acquire);
        if (gen == _applied_gen) return;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            _active      = _pending;
            _applied_gen = gen;
        }
        _active.window_samples     = clamp_window_at(_active.window_samples, _active.sample_rate);
        _active.pretrigger_samples = clamp_pre(_active.pretrigger_samples, _active.window_samples);
        _active.stride             = stride_for(_active.window_samples);
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
    void reset_ring()       { _ring_w = 0; _ring_fill = 0; _ring_acc_n = 0; }
    void reset_edge_latch() { _armed_band = false; }

    // Points the pre-trigger ring holds at the active config.
    size_t ring_points() const {
        return points_for(_active.pretrigger_samples, _active.stride);
    }

    inline void ring_push(T s) {
        const size_t pre = _active.pretrigger_samples;
        if (pre == 0) return;
        const size_t stride = _active.stride;
        if (stride <= 1) {
            _ring[_ring_w] = s;
            _ring_w = (_ring_w + 1) % pre;
            if (_ring_fill < pre) _ring_fill++;
            return;
        }
        if (_ring_acc_n == 0) { _ring_acc_min = s; _ring_acc_max = s; }
        else { _ring_acc_min = std::min(_ring_acc_min, s); _ring_acc_max = std::max(_ring_acc_max, s); }
        if (++_ring_acc_n < stride) return;
        _ring_acc_n = 0;
        const size_t cap = ring_points();          // even; _ring_w stays even
        _ring[_ring_w]     = _ring_acc_min;
        _ring[_ring_w + 1] = _ring_acc_max;
        _ring_w = (_ring_w + 2) % cap;
        if (_ring_fill < cap) _ring_fill += 2;
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
        const size_t pre    = _active.pretrigger_samples;
        const size_t stride = _active.stride;
        const size_t cap    = points_for(pre, stride);         // ring capacity in points
        size_t have = (pre == 0) ? 0 : std::min(_ring_fill, cap);
        for (size_t j = 0; j < have; ++j)
            _capture[j] = _ring[(_ring_w + cap - have + j) % cap];   // oldest first

        // Flush the ring's partial bucket. The sample that FIRED the trigger is
        // in it (ring_push runs before the edge test), so dropping it would let
        // a burst shorter than one bucket trigger a frame it never appears in.
        // clamp_pre() leaves >=1 post-trigger sample, so the pair always fits.
        if (stride > 1 && _ring_acc_n > 0) {
            _capture[have]     = _ring_acc_min;
            _capture[have + 1] = _ring_acc_max;
            have += 2;
        }
        _ring_acc_n = 0;

        const size_t post = _active.window_samples - pre;
        _capture_fill = have;
        _capture_len  = std::min(have + points_for(post, stride), _capacity);
        // A flushed partial counts as a whole bucket here, overstating the
        // pre-trigger span by less than one bucket -- below the resolution the
        // frame is stored at, and pre_ms/post_ms come from the config anyway.
        _capture_trig = (stride <= 1) ? have : (have / 2) * stride;   // input samples
        _cap_acc_n    = 0;
        _auto_counter = 0;
        if (_capture_fill >= _capture_len) publish_frame();
        else _state.store(State::Capturing, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> fill_capture() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        const size_t stride = _active.stride;

        if (stride <= 1) {
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

        const T* p1; const T* p2; size_t s1, s2;
        in.peek_read(p1, s1, p2, s2);
        size_t n = std::min(avail, s1 + s2);
        size_t consumed = 0;
        for (size_t i = 0; i < n && _capture_fill < _capture_len; ++i) {
            T s = (i < s1) ? p1[i] : p2[i - s1];
            if (_cap_acc_n == 0) { _cap_acc_min = s; _cap_acc_max = s; }
            else { _cap_acc_min = std::min(_cap_acc_min, s); _cap_acc_max = std::max(_cap_acc_max, s); }
            if (++_cap_acc_n >= stride) {
                _cap_acc_n = 0;
                _capture[_capture_fill]     = _cap_acc_min;
                _capture[_capture_fill + 1] = _cap_acc_max;
                _capture_fill += 2;
            }
            ring_push(s);
            ++consumed;
        }
        in.commit_read(consumed);
        if (_capture_fill >= _capture_len) publish_frame();
        return consumed ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                        : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSamples);
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
            _snap_stride   = _active.stride;
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
    float   _max_window_ms = 0.0f;   // time ceiling on the window; rate-independent
    size_t  _capacity      = 0;      // stored points per frame (NOT input samples)
    T*      _capture = nullptr;
    T*      _ring    = nullptr;

    // Snapshot shared with the GUI thread; guarded by _snap_mutex.
    std::mutex    _snap_mutex;
    float*        _snap_buf = nullptr;          // written by the block thread under lock
    size_t        _snap_len = 0;                // stored POINTS, not input samples
    size_t        _snap_trig_idx = 0;           // trigger offset in INPUT SAMPLES
    size_t        _snap_stride = 1;             // input samples per (min,max) pair
    float         _snap_pre_ms = 0.0f;
    float         _snap_post_ms = 0.0f;
    float         _snap_level = 0.0f;
    size_t        _snap_rate = 1;   // rate the published frame was captured at
    unsigned long _frame_count = 0;
    // Lock-free mirror of _frame_count for frame_count(); written by the block
    // thread just after publish_frame() releases _snap_mutex.
    std::atomic<unsigned long> _frames_published{0};

    static constexpr size_t MAX_PLOT_POINTS     = 8000;  // large windows decimate to a min/max envelope of this many points
    // Point budget across the 4 frame buffers at 4 B/point => ~32 MB. Sized far
    // above MAX_PLOT_POINTS so an enveloped frame still outresolves the display
    // by ~2 orders of magnitude, and zoom/export keep detail the plot drops.
    static constexpr size_t MAX_CAPTURE_POINTS  = 2u * 1024 * 1024;
    static constexpr size_t MIN_CAPTURE_POINTS  = 16384;   // never starve the display

    // GUI-thread-only render copies.
    float*        _render_y = nullptr;
    float*        _x_render = nullptr;
    float*        _plot_x = nullptr;   // decimated envelope x (large windows)
    float*        _plot_y = nullptr;   // decimated envelope y
    size_t        _render_len = 0, _render_trig = 0;
    size_t        _render_rate = 1, _render_stride = 1;
    float         _render_pre_ms = 0.0f, _render_post_ms = 0.0f, _render_level = 0.0f;
    unsigned long _render_frame = 0;

    // Block-thread-only runtime state. _ring_w/_ring_fill count POINTS;
    // _capture_trig is an INPUT-SAMPLE offset (see frame_input_index).
    size_t  _ring_w = 0, _ring_fill = 0;
    size_t  _capture_fill = 0, _capture_len = 0, _capture_trig = 0;
    size_t  _holdoff_counter = 0, _auto_counter = 0;
    bool    _armed_band = false;
    // Envelope accumulators: the ring and the post-trigger capture bucket
    // independently (the ring keeps running across a capture).
    size_t  _ring_acc_n = 0, _cap_acc_n = 0;
    T       _ring_acc_min{}, _ring_acc_max{}, _cap_acc_min{}, _cap_acc_max{};

    ImVec2 _win_pos{380.0f, 10.0f};
    ImVec2 _win_size{1100.0f, 430.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos{0.0f, 0.0f};
    ImVec2 _pending_rect_size{0.0f, 0.0f};
};
