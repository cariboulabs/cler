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

    // Enable/disable heavy processing from outside (e.g. when the window is
    // hidden). While inactive the block still fully drains its input each call
    // (so it never stalls an upstream fanout) but does no FFT / row work.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    // How many FFT frames are collapsed (peak-held) into one waterfall row.
    // Larger => each row spans more time => longer total history on screen.
    void   set_frames_per_row(size_t n) { _frames_per_row.store(n < 1 ? 1 : n); }
    size_t frames_per_row() const { return _frames_per_row.load(); }

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
    // with no full-buffer memmove. The expensive reorder into display order is
    // done once per render (see _display) instead of once per incoming frame.
    float** _spectrograms;
    size_t  _ring_pos   = 0;   // next row to overwrite (chronological write head)
    size_t  _ring_count = 0;   // valid rows so far (saturates at _tall)

    // Display buffers assembled at render time, newest row first (row 0).
    float** _display; // [num_inputs][tall * n_fft_samples]

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

    std::mutex _spectrogram_mutex;

    // GUI
    ImVec2 _initial_window_position = ImVec2(200, 200);
    ImVec2 _initial_window_size = ImVec2(600, 400);

    std::atomic<bool> _gui_pause = false;
};
