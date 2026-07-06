#include "plot_cspectrogram.hpp"
#include "implot.h"
#include <cstring>

PlotCSpectrogramBlock::PlotCSpectrogramBlock(const char*name,
    const std::vector<std::string> signal_labels,
    size_t sps,
    size_t n_fft_samples,
    size_t tall,
    SpectralWindow window_type)
    : BlockBase(name),
      _num_inputs(signal_labels.size()),
      _signal_labels(std::move(signal_labels)),
      _sps(sps),
      _n_fft_samples(n_fft_samples),
      _tall(tall),
      _window_type(window_type)
{
    if (_num_inputs < 1) throw std::invalid_argument("At least one input required");
    if (_n_fft_samples <= 2 || _n_fft_samples % 2 != 0) throw std::invalid_argument("FFT size must be even and > 2");
    if (_tall < 1) throw std::invalid_argument("Tall must be > 0");

    in = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    
    // Calculate buffer size with better DBF compatibility
    size_t min_buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
    
    // Use standard multiplier, but ensure we meet DBF requirements for small FFTs
    size_t buffer_multiplier = BUFFER_SIZE_MULTIPLIER;  // Default is 3
    if (_n_fft_samples < min_buffer_size) {
        // For small FFT sizes, increase multiplier to ensure adequate buffering
        // This ensures smooth operation with high-throughput DBF sources
        buffer_multiplier = std::max(
            BUFFER_SIZE_MULTIPLIER,
            (2 * min_buffer_size + _n_fft_samples - 1) / _n_fft_samples
        );
    }
    
    size_t buffer_size = std::max(
        buffer_multiplier * _n_fft_samples,
        min_buffer_size
    );
    
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<std::complex<float>>(buffer_size);
    }

    _liquid_inout = new std::complex<float>[_n_fft_samples];
    _tmp_y_buffer = new std::complex<float>[_n_fft_samples];
    _tmp_mag_buffer = new float[_n_fft_samples];

    _spectrograms = new float*[_num_inputs];
    _display      = new float*[_num_inputs];
    _accum        = new float*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _spectrograms[i] = new float[_tall * _n_fft_samples];
        std::fill_n(_spectrograms[i], _tall * _n_fft_samples, -147.0f);
        _display[i] = new float[_tall * _n_fft_samples];
        std::fill_n(_display[i], _tall * _n_fft_samples, -147.0f);
        _accum[i] = new float[_n_fft_samples];
    }

    _freq_bins = new float[_n_fft_samples];
    for (size_t i = 0; i < _n_fft_samples; ++i) {
        _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(_n_fft_samples))) - (_sps / 2.0f);
    }

    _fftplan = fft_create_plan(_n_fft_samples,
        reinterpret_cast<liquid_float_complex*>(_liquid_inout),
        reinterpret_cast<liquid_float_complex*>(_liquid_inout),
        LIQUID_FFT_FORWARD, 0);
}

PlotCSpectrogramBlock::~PlotCSpectrogramBlock() {
    using ComplexChannel = cler::Channel<std::complex<float>>;
    for (size_t i = 0; i < _num_inputs; ++i) {
        in[i].~ComplexChannel();
    }
    ::operator delete[](in);

    delete[] _liquid_inout;
    delete[] _tmp_y_buffer;
    delete[] _tmp_mag_buffer;

    for (size_t i = 0; i < _num_inputs; ++i) {
        delete[] _spectrograms[i];
        delete[] _display[i];
        delete[] _accum[i];
    }
    delete[] _spectrograms;
    delete[] _display;
    delete[] _accum;

    delete[] _freq_bins;
    fft_destroy_plan(_fftplan);
}

cler::Result<cler::Empty, cler::Error> PlotCSpectrogramBlock::procedure() {
    bool paused = _gui_pause.load(std::memory_order_acquire) ||
                  _external_pause.load(std::memory_order_acquire);

    // Work in whole FFT frames, bounded by the input with the fewest samples so
    // every input advances by the same number of rows (they share the ring head).
    size_t available = in[0].size();
    for (size_t i = 1; i < _num_inputs; ++i) {
        if (in[i].size() < available) available = in[i].size();
    }
    size_t frames_avail = available / _n_fft_samples;
    if (frames_avail == 0) {
        return cler::Error::NotEnoughSamples;
    }

    // When paused (or hidden), just drain the inputs without updating the
    // spectrogram so the upstream fanout never backs up (which would stall
    // sibling branches, e.g. the trigger). Drop any partially-filled row.
    if (paused) {
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].commit_read(frames_avail * _n_fft_samples);
        }
        _accum_count = 0;
        return cler::Empty{};
    }

    // Always drain the whole channel, but only FFT the most recent frames. Older
    // frames beyond the cap are dropped (decimated) rather than left to overflow.
    size_t frames_to_fft  = std::min(frames_avail, MAX_FFTS_PER_CALL);
    size_t frames_to_drop = frames_avail - frames_to_fft;
    size_t fpr = _frames_per_row.load(std::memory_order_relaxed);
    if (fpr < 1) fpr = 1;

    std::lock_guard<std::mutex> lock(_spectrogram_mutex);

    for (size_t frame = 0; frame < frames_avail; ++frame) {
        // Load one frame from each input (dropping the oldest, then FFT the rest).
        bool do_fft = frame >= frames_to_drop;
        bool first_in_row = (_accum_count == 0);

        for (size_t i = 0; i < _num_inputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            const std::complex<float>* src;
            if (read_ptr && read_size >= _n_fft_samples) {
                src = read_ptr;                       // FAST PATH: read in place
            } else {
                in[i].readN(_tmp_y_buffer, _n_fft_samples);
                src = _tmp_y_buffer;
            }

            if (do_fft) {
                float coherent_gain = 0.0f;
                for (size_t n = 0; n < _n_fft_samples; ++n) {
                    float w = spectral_window_function(_window_type, n / static_cast<float>(_n_fft_samples - 1));
                    coherent_gain += w;
                    // (-1)^n centers DC (equivalent to an fftshift of the output).
                    _liquid_inout[n] = src[n] * (w * ((n % 2 == 0) ? 1.0f : -1.0f));
                }
                coherent_gain /= static_cast<float>(_n_fft_samples);

                fft_execute(_fftplan);

                float scale = static_cast<float>(_n_fft_samples) * coherent_gain;
                float scale2 = scale * scale;

                // Peak-hold this frame into the accumulator row for this input.
                float* acc = _accum[i];
                for (size_t j = 0; j < _n_fft_samples; ++j) {
                    float re = _liquid_inout[j].real();
                    float im = _liquid_inout[j].imag();
                    float db = 10.0f * log10f((re * re + im * im) / scale2 + 1e-20f);
                    acc[j] = first_in_row ? db : std::max(acc[j], db);
                }
            }

            if (read_ptr && read_size >= _n_fft_samples) {
                in[i].commit_read(_n_fft_samples);    // matching commit for FAST PATH
            }
        }

        if (do_fft && ++_accum_count >= fpr) {
            // Flush the peak-held accumulator into a new ring row (O(n_fft),
            // no memmove). All inputs share the same ring head.
            for (size_t i = 0; i < _num_inputs; ++i) {
                memcpy(_spectrograms[i] + _ring_pos * _n_fft_samples,
                       _accum[i], _n_fft_samples * sizeof(float));
            }
            _ring_pos = (_ring_pos + 1) % _tall;
            if (_ring_count < _tall) ++_ring_count;
            _accum_count = 0;
        }
    }

    return cler::Empty{};
}

void PlotCSpectrogramBlock::render() {
    // FirstUseEver (not Always) so the user can freely resize the window; Always
    // snapped it back to the initial size on every frame.
    ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    ImGui::Begin(name());

    const ImPlotAxisFlags x_flags = ImPlotAxisFlags_Lock;
    const ImPlotAxisFlags y_flags = ImPlotAxisFlags_Lock;

    if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
        _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
    }

    // Time-span control: how many FFT frames are peak-held into one row. This
    // stretches the total history shown without reallocating the ring.
    ImGui::SameLine();
    int fpr = static_cast<int>(_frames_per_row.load());
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderInt("frames/row", &fpr, 1, 256, "%d", ImGuiSliderFlags_Logarithmic)) {
        _frames_per_row.store(static_cast<size_t>(fpr < 1 ? 1 : fpr));
    }
    if (_sps > 0) {
        float span_s = static_cast<float>(_tall) * static_cast<float>(fpr)
                     * static_cast<float>(_n_fft_samples) / static_cast<float>(_sps);
        ImGui::SameLine();
        ImGui::Text("~%.1f s history", span_s);
    }

    // Seconds per waterfall row at the CURRENT frames/row setting. Rows already
    // in the ring may have been produced under a different setting, so the time
    // axis is exact only for rows written since the last change -- the same
    // caveat as the "~N s history" text above. Falls back to row units if the
    // sample rate is unknown.
    const double row_dt = (_sps > 0)
        ? static_cast<double>(fpr) * static_cast<double>(_n_fft_samples)
              / static_cast<double>(_sps)
        : 1.0;

    {
        // Reorder the chronological ring into display order (newest row first).
        // This is the only O(tall * n_fft) copy, and it happens at render rate
        // (~50 Hz) rather than on the data path per incoming FFT frame.
        std::lock_guard<std::mutex> lock(_spectrogram_mutex);
        for (size_t i = 0; i < _num_inputs; ++i) {
            for (size_t k = 0; k < _tall; ++k) {
                float* dst = _display[i] + k * _n_fft_samples;
                if (k < _ring_count) {
                    size_t src_row = (_ring_pos + _tall - 1 - k) % _tall;
                    memcpy(dst, _spectrograms[i] + src_row * _n_fft_samples,
                           _n_fft_samples * sizeof(float));
                } else {
                    std::fill_n(dst, _n_fft_samples, -147.0f);
                }
            }
        }
    }

    for (size_t i = 0; i < _num_inputs; ++i) {
        if (ImPlot::BeginPlot(_signal_labels[i].c_str(), ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Frequency (Hz)", "Time (s)", x_flags, y_flags);
            ImPlot::SetupAxisLimits(ImAxis_X1, -static_cast<double>(_sps)/2.0, static_cast<double>(_sps)/2.0);
            // Elapsed time in seconds, 0 s (newest) at the top. frames/row can
            // change live, which rescales the axis, so this must be
            // ImPlotCond_Always -- safe here because the axis is Lock'ed, so
            // Always cannot fight user pan/zoom.
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(_tall) * row_dt, 0.0,
                                    ImPlotCond_Always);
            ImPlot::PushColormap(ImPlotColormap_Plasma);

            std::string label = "##" + std::string(_signal_labels[i]);
            ImPlot::PlotHeatmap(
                label.c_str(),
                _display[i],
                _tall,
                _n_fft_samples,
                0.0, 0.0,
                nullptr,
                ImPlotPoint(-static_cast<double>(_sps)/2.0, static_cast<double>(_tall) * row_dt),
                ImPlotPoint(static_cast<double>(_sps)/2.0, 0)
            );
            ImPlot::PopColormap();
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

void PlotCSpectrogramBlock::set_initial_window(float x, float y, float w, float h) {
    _initial_window_position = ImVec2(x, y);
    _initial_window_size = ImVec2(w, h);
}
