#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "imgui.h"
#include "implot.h"
#include "spectral_windows.hpp"
#include <mutex>
#include <vector>

struct PlotCSpectrogramBlock : public cler::BlockBase {
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    // Upper bound on FFTs computed per procedure() call. If the source outruns
    // us (e.g. very high sample rates), we still drain the whole input channel
    // every call so the upstream fanout never stalls, but only turn the most
    // recent MAX_FFTS_PER_CALL frames into waterfall rows. This bounds both CPU
    // and the time the render lock is held.
    static constexpr size_t MAX_FFTS_PER_CALL = 32;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrogramBlock(const char* name,
                          const std::vector<std::string> signal_labels,
                          size_t sps,
                          size_t n_fft_samples,
                          size_t tall,
                          SpectralWindow window_type = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrogramBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

    // One-shot programmatic window rect: the next render() applies it with
    // ImGuiCond_Always and clears the request, so the user can still move or
    // resize the window afterward. Both the setter and render() run on the GUI
    // thread (procedure() never touches these), so plain members are fine.
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    // Enable/disable heavy processing from outside (e.g. when the window is
    // hidden). While inactive the block still fully drains its input each call
    // (so it never stalls an upstream fanout) but does no FFT / row work.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    // GUI-THREAD-ONLY: retune the frequency/time axes to a new sample rate.
    // _sps is consumed only in render()/export_display(), which run on the
    // same (GUI) thread as this setter, so its plain write is safe -- call
    // from the GUI thread only. Rows already in the ring were recorded at the
    // OLD rate and would be silently mislabeled on the new frequency axis, so
    // the ring is CLEARED here (under _spectrogram_mutex, which also guards
    // the row flush in procedure()); the waterfall restarts empty. Also
    // requests a one-shot X-axis re-fit on the next render().
    void set_sample_rate(size_t sps);

    // How many FFT frames are collapsed (peak-held) into one waterfall row.
    // Larger => each row spans more time => longer total history on screen.
    void   set_frames_per_row(size_t n) { _frames_per_row.store(n < 1 ? 1 : n); }
    size_t frames_per_row() const { return _frames_per_row.load(); }

    // Measurement-grid overlay. All of these are read/written only on the GUI
    // thread: the "Grid" checkbox lives in this block's own window (render()),
    // while the line spacing is driven from the control panel. Plain members
    // are therefore fine (no atomics needed). Spacing is in user units: time
    // between horizontal lines in MILLISECONDS, frequency between vertical
    // lines in MHz (anchored at DC / band center).
    void  set_show_grid(bool on)       { _show_grid = on; }
    bool  show_grid() const            { return _show_grid; }
    void  set_grid_time_ms(float ms)   { _grid_time_ms = ms; }
    float grid_time_ms() const         { return _grid_time_ms; }
    void  set_grid_freq_mhz(float mhz) { _grid_freq_mhz = mhz; }
    float grid_freq_mhz() const        { return _grid_freq_mhz; }

    // GUI-THREAD-ONLY export of one channel's waterfall: `data` gets rows*cols
    // floats (dB), row-major, NEWEST row first (rows beyond the fill level are
    // padded with the floor value) -- the same image the plot shows. The
    // chronological ring is reordered on demand here, under _spectrogram_mutex
    // (snapshots are rare; the per-frame render path no longer reorders at
    // all). Also reports the current frames-per-row and sample rate so one
    // row's time span can be computed as frames_per_row * cols / sps seconds.
    // Returns false if no row has been recorded yet or `channel` is out of
    // range. May allocate (resizes `data`); GUI thread only (_sps is a plain
    // member owned by that thread).
    bool export_display(size_t channel, std::vector<float>& data,
                        size_t& rows, size_t& cols,
                        size_t& frames_per_row_out, size_t& sps_out) const;

private:
    size_t _num_inputs;
    std::vector<std::string> _signal_labels;

    size_t _sps;
    size_t _n_fft_samples;
    size_t _tall;
    SpectralWindow _window_type;

    std::complex<float>* _liquid_inout;
    std::complex<float>* _tmp_y_buffer;
    float* _tmp_mag_buffer;

    // Row-major ring of waterfall rows: [num_inputs][tall * n_fft_samples].
    // New rows are written at _ring_pos (mod _tall) on the data path in O(n_fft)
    // with no full-buffer memmove. render() mirrors new rows into a GL texture
    // (same ring layout) and draws it seam-split, so nothing ever reorders the
    // ring on the frame path; export_display() reorders on demand for snapshots.
    float** _spectrograms;
    size_t  _ring_pos   = 0;   // next row to overwrite (chronological write head)
    size_t  _ring_count = 0;   // valid rows so far (saturates at _tall)

    // Row generation counter: bumped once per new ring row, inside the same
    // _spectrogram_mutex critical section as the row write in procedure().
    // render() compares it (under the mutex) against its last-seen value and
    // only copies out / colorizes / uploads texture rows when new rows landed
    // since the previous frame; on the steady path with no new rows the frame
    // is just two PlotImage quads.
    size_t  _row_gen = 0;                             // guarded by _spectrogram_mutex
    size_t  _row_gen_seen = static_cast<size_t>(-1);  // GUI thread only

    // ---- GL-texture waterfall renderer (GUI thread only, incl. all GL) ----
    // One RGBA8 texture per input, n_fft wide x tall high, rows stored at
    // their RING positions (chronological order, same layout as
    // _spectrograms). render() copies rows that arrived since the last frame
    // out under _spectrogram_mutex (into _stage), then -- after unlocking --
    // colorizes them through a 256-entry colormap LUT and glTexSubImage2D's
    // only those rows in place. The plot draws the ring as up to two
    // ImPlot::PlotImage quads split at the ring seam, so the old
    // O(tall * n_fft) ring->display reorder and the per-frame per-cell
    // PlotHeatmap rasterization are both gone from the render path.
    //
    // Color scale: the old PlotHeatmap call used scale_min == scale_max == 0,
    // i.e. ImPlot's auto-scale from the data min/max each frame. Exact
    // per-frame auto-scale would force a full recolor+re-upload whenever the
    // min/max drifts (practically every frame), so the texture bakes the scale
    // with a small dB margin and hysteresis: it is re-derived (full recolor)
    // only when the data leaves the baked range or the range shrinks
    // materially. Per-ring-row min/max are tracked to make that decision O(tall).
    static constexpr float DB_FLOOR = -147.0f;  // empty-row fill value (dB)
    unsigned int* _tex = nullptr;  // [num_inputs] GL texture names (0 until created)
    bool          _lut_built = false;
    ImU32         _lut[256];       // Plasma LUT (same colormap PlotHeatmap used)
    float**       _stage;          // [num_inputs][tall * n_fft] rows copied out under the mutex
    ImU32*        _pixels;         // [tall * n_fft] RGBA8 upload staging (shared across inputs)
    float**       _row_min;        // [num_inputs][tall] per-ring-row dB min (DB_FLOOR if unwritten)
    float**       _row_max;        // [num_inputs][tall] per-ring-row dB max
    float*        _scale_min;      // [num_inputs] dB->color scale baked into the texture
    float*        _scale_max;      //   (min == max means "flat": solid colormap color 0)
    bool*         _needs_full;     // [num_inputs] per-frame scratch: full recolor this frame?
    bool          _tex_full_dirty = true;  // force full recolor+upload on next render()
    size_t        _tex_ring_pos   = 0;     // ring snapshot the texture contents reflect;
    size_t        _tex_ring_count = 0;     //   also used to place the two seam quads

    // Peak-hold accumulator: several incoming FFT frames are max-combined here
    // and flushed to one ring row every _frames_per_row frames. This lets the
    // waterfall cover a long time span (controllable, no reallocation) while
    // still catching transient bursts that a plain drop-decimation would miss.
    float** _accum;              // [num_inputs][n_fft_samples]
    size_t  _accum_count = 0;    // frames folded into the current row so far
    std::atomic<size_t> _frames_per_row{1};

    std::atomic<bool> _external_pause{false};

    float* _freq_bins;

    fftplan _fftplan;

    // mutable: export_display() is const but must lock against the DSP thread
    // now that it reads the ring directly (render() no longer keeps a
    // GUI-owned reordered copy).
    mutable std::mutex _spectrogram_mutex;

    // GUI
    ImVec2 _initial_window_position = ImVec2(200, 200);
    ImVec2 _initial_window_size = ImVec2(600, 400);

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos  = ImVec2(0, 0);
    ImVec2 _pending_rect_size = ImVec2(0, 0);

    // One-shot X-axis re-fit after set_sample_rate() (GUI thread only).
    bool   _axis_refit = false;

    // Paused-view zoom (GUI thread only). While paused the image is drawn with
    // the row_dt captured when pausing began, so the frozen capture stays fixed
    // in true seconds while the Y axis keeps tracking the live History value --
    // dragging History then zooms the viewport around the pinned data (and its
    // grid) instead of scaling image and axis together into a static-looking
    // picture. Reset implicitly whenever a fresh pause is entered.
    bool   _was_paused    = false;
    double _paused_row_dt = 0.0;

    // Measurement-grid overlay state (GUI thread only; see the setters above).
    bool   _show_grid     = false;   // "Grid" checkbox in this window
    float  _grid_time_ms  = 100.0f;  // horizontal (time) line spacing, ms
    float  _grid_freq_mhz = 1.0f;    // vertical (frequency) line spacing, MHz

    std::atomic<bool> _gui_pause = false;
};
