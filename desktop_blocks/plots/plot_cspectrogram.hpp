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

    // Input is always fully drained (upstream fanout never stalls); only the
    // most recent MAX_FFTS_PER_CALL frames become rows, bounding CPU/lock time.
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

    // One-shot: next render() applies this rect (ImGuiCond_Always) then clears
    // the request, so the user can still move/resize afterward.
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    // While inactive, input is still fully drained (no upstream stall) but no
    // FFT/row work happens.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    // GUI-THREAD-ONLY. Existing ring rows were recorded at the old rate and
    // would be mislabeled on the new axis, so this clears the ring (under
    // _spectrogram_mutex) and the waterfall restarts empty.
    void set_sample_rate(size_t sps);

    // How many FFT frames are collapsed (peak-held) into one waterfall row.
    // Larger => each row spans more time => longer total history on screen.
    void   set_frames_per_row(size_t n) { _frames_per_row.store(n < 1 ? 1 : n); }
    size_t frames_per_row() const { return _frames_per_row.load(); }

    // Measurement-grid overlay (GUI thread only). Spacing: time between
    // horizontal lines in ms, frequency between vertical lines in MHz.
    void  set_show_grid(bool on)       { _show_grid = on; }
    bool  show_grid() const            { return _show_grid; }
    void  set_grid_time_ms(float ms)   { _grid_time_ms = ms; }
    float grid_time_ms() const         { return _grid_time_ms; }
    void  set_grid_freq_mhz(float mhz) { _grid_freq_mhz = mhz; }
    float grid_freq_mhz() const        { return _grid_freq_mhz; }

    // GUI-THREAD-ONLY. `data` gets rows*cols dB floats, row-major, NEWEST row
    // first (unfilled rows padded with DB_FLOOR); one row spans
    // frames_per_row_out * cols / sps_out seconds. False if no row recorded yet
    // or `channel` out of range.
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
    // Nothing reorders it on the frame path; export_display() reorders on
    // demand for snapshots.
    float** _spectrograms;
    size_t  _ring_write_pos = 0;   // next row to overwrite, mod _tall
    size_t  _ring_count = 0;       // valid rows so far (saturates at _tall)

    // Bumped once per new ring row, inside the row-write's _spectrogram_mutex
    // section; render() diffs against its last-seen value to skip texture work
    // when no new rows landed.
    size_t  _row_gen = 0;                             // guarded by _spectrogram_mutex
    size_t  _row_gen_seen = static_cast<size_t>(-1);  // GUI thread only

    // GL waterfall texture (GUI thread only, incl. all GL): one RGBA8 texture
    // per input, rows at their ring positions. render() copies new rows out
    // under _spectrogram_mutex, then colorizes/uploads after unlocking (never
    // holds the DSP-facing lock across GL calls). Color scale is baked into
    // the texture with a dB margin and hysteresis (see .cpp) rather than
    // rescaled every frame; per-ring-row min/max make that decision O(tall).
    static constexpr float DB_FLOOR = -147.0f;  // empty-row fill value (dB)
    unsigned int* _tex = nullptr;  // [num_inputs] GL texture names (0 until created)
    bool          _lut_built = false;
    ImU32         _lut[256];       // Plasma colormap LUT
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

    // Peak-hold accumulator: FFT frames are max-combined here, flushed to one
    // ring row every _frames_per_row frames.
    float** _accum;              // [num_inputs][n_fft_samples]
    size_t  _accum_count = 0;    // frames folded into the current row so far
    std::atomic<size_t> _frames_per_row{1};

    std::atomic<bool> _external_pause{false};

    float* _freq_bins;

    fftplan _fftplan;

    // mutable: export_display() is const but must lock against the DSP thread.
    mutable std::mutex _spectrogram_mutex;

    ImVec2 _initial_window_position = ImVec2(200, 200);
    ImVec2 _initial_window_size = ImVec2(600, 400);

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos  = ImVec2(0, 0);
    ImVec2 _pending_rect_size = ImVec2(0, 0);

    // One-shot X-axis re-fit after set_sample_rate() (GUI thread only).
    bool   _axis_refit = false;

    // Paused-view zoom (GUI thread only): image keeps the row_dt captured at
    // pause time while the Y axis tracks the live History value, so dragging
    // History zooms the viewport around the frozen data instead of scaling both.
    bool   _was_paused    = false;
    double _paused_row_dt = 0.0;

    // Measurement-grid overlay state (GUI thread only; see the setters above).
    bool   _show_grid     = false;   // "Grid" checkbox in this window
    float  _grid_time_ms  = 100.0f;  // horizontal (time) line spacing, ms
    float  _grid_freq_mhz = 1.0f;    // vertical (frequency) line spacing, MHz

    std::atomic<bool> _gui_pause = false;
};
