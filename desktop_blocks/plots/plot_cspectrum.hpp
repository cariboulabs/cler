#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "spectral_windows.hpp"
#include "imgui.h"
#include <vector>
#include <mutex>
#include <vector>
#include <type_traits>

struct PlotCSpectrumBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    const size_t BUFFER_SIZE_MULTIPLIER = 3;
    static constexpr size_t MAX_INPUT_CHANNEL_SLOTS = 16;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(const char* name,
                       const std::vector<std::string>& signal_labels,
                       size_t sps,
                       size_t n_fft_samples,
                       SpectralWindow window_type = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrumBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

    // One-shot: next render() applies this rect (ImGuiCond_Always) then clears
    // the request, so the user can still move/resize afterward.
    void apply_window_rect(float x, float y, float w, float h);

    // While INACTIVE, procedure() still drains input each call (no upstream
    // stall) but DROPS the samples instead of ringing them. Distinct from the
    // Pause button (_gui_pause), which freezes the display while data still
    // flows through.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    void set_visible(bool visible) { _visible = visible; }

    // GUI-THREAD-ONLY: retunes the frequency axis and requests a one-shot
    // X-axis re-fit on the next render(). procedure() never reads _sps.
    void set_sample_rate(size_t sps);

    // GUI-THREAD-ONLY export of the currently displayed (averaged) spectrum:
    // freq_hz is baseband Hz (size n_fft), mag_db the averaged magnitudes.
    // False if render() hasn't produced a spectrum yet or channel is invalid.
    bool export_spectrum(size_t channel, std::vector<float>& freq_hz,
                         std::vector<float>& mag_db) const;

private:
    void next_window_geometry();   // SetNextWindowPos/Size before Begin()

    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;
    size_t _buffer_size;
    SpectralWindow _window_type;

    cler::Channel<std::complex<float>>* _signal_channels;

    std::aligned_storage_t<sizeof(cler::Channel<std::complex<float>>), alignof(cler::Channel<std::complex<float>>)> _in_storage[MAX_INPUT_CHANNEL_SLOTS];

    std::complex<float>** _snapshot_buffers = nullptr;
    std::complex<float>* _tmp_buffer = nullptr;

    std::complex<float>* _liquid_inout = nullptr;
    float* _freq_bins = nullptr;
    float* _tmp_mag_buffer = nullptr;
    float** _spectrum_avg = nullptr;  // Averaged spectrum for each input
    float _avg_alpha = 0.7f;          // Exponential averaging factor (0=frozen, 1=no averaging)
    bool _first_spectrum = true;

    fftplan _fftplan;

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos {0.0f, 0.0f};
    ImVec2 _pending_rect_size {0.0f, 0.0f};

    // One-shot X-axis re-fit after set_sample_rate() (GUI thread only).
    bool   _axis_refit = false;

    std::mutex _snapshot_mutex;
    size_t _snapshot_ready_size = 0;

    std::atomic<bool> _gui_pause = false;

    std::atomic<bool> _external_pause{false};

    bool _visible = true;
};
