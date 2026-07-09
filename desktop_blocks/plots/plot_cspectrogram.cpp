#include "plot_cspectrogram.hpp"
#include "cler_utils.hpp"
#include "implot.h"
#include <cmath>
#include <cstring>
#include <cfloat>

// Raw GL for the waterfall texture. Same idiom as gui_manager.cpp: the GLFW
// header pulls in the platform's GL 1.1 headers (all we need: glGenTextures /
// glTexSubImage2D / glDeleteTextures), and blocks_gui links OpenGL::GL.
// glfwGetCurrentContext() is also used to guard texture deletion at shutdown.
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE   // GL 1.2 enum; not in every bare gl.h
#define GL_CLAMP_TO_EDGE 0x812F
#endif

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
    if (_num_inputs < 1) cler::panic("At least one input required");
    if (_n_fft_samples <= 2 || _n_fft_samples % 2 != 0) cler::panic("FFT size must be even and > 2");
    if (_tall < 1) cler::panic("Tall must be > 0");

    in = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );

    size_t min_buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);

    // Below the DBF minimum, bump the multiplier so the buffer still clears it.
    size_t buffer_multiplier = BUFFER_SIZE_MULTIPLIER;
    if (_n_fft_samples < min_buffer_size) {
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
    _accum        = new float*[_num_inputs];
    _stage        = new float*[_num_inputs];
    _row_min      = new float*[_num_inputs];
    _row_max      = new float*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _spectrograms[i] = new float[_tall * _n_fft_samples];
        std::fill_n(_spectrograms[i], _tall * _n_fft_samples, DB_FLOOR);
        _accum[i] = new float[_n_fft_samples];
        _stage[i] = new float[_tall * _n_fft_samples];
        _row_min[i] = new float[_tall];
        _row_max[i] = new float[_tall];
        std::fill_n(_row_min[i], _tall, DB_FLOOR);
        std::fill_n(_row_max[i], _tall, DB_FLOOR);
    }
    _pixels     = new ImU32[_tall * _n_fft_samples];
    _tex        = new unsigned int[_num_inputs]();  // 0 = not created yet
    _scale_min  = new float[_num_inputs]();
    _scale_max  = new float[_num_inputs]();
    _needs_full = new bool[_num_inputs]();

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
        delete[] _accum[i];
        delete[] _stage[i];
        delete[] _row_min[i];
        delete[] _row_max[i];
    }
    delete[] _spectrograms;
    delete[] _accum;
    delete[] _stage;
    delete[] _row_min;
    delete[] _row_max;
    delete[] _pixels;
    delete[] _scale_min;
    delete[] _scale_max;
    delete[] _needs_full;

    // GL textures may only be deleted while a GL context is current on this
    // thread. Some examples construct this block BEFORE GuiManager (e.g.
    // hackrf_spectrum), so at scope exit the context -- and GLFW itself -- may
    // already be gone; glfwGetCurrentContext() then returns NULL (it is safe
    // to call even after glfwTerminate) and we deliberately leak the textures
    // at process exit rather than crash on a dead context.
    if (glfwGetCurrentContext() != nullptr) {
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (_tex[i] != 0) glDeleteTextures(1, &_tex[i]);
        }
    }
    delete[] _tex;

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
            ++_row_gen;   // tell render() the ring changed (we hold the mutex)
            _accum_count = 0;
        }
    }

    return cler::Empty{};
}

void PlotCSpectrogramBlock::render() {
    // Apply a pending programmatic rect exactly once (Always, then clear the
    // flag) so the user can still move/resize afterward. The default path
    // stays FirstUseEver (not Always): Always every frame snapped the window
    // back to the initial size and blocked manual resizing.
    if (_pending_rect) {
        ImGui::SetNextWindowPos(_pending_rect_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(_pending_rect_size, ImGuiCond_Always);
        _pending_rect = false;
    } else {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    }
    ImGui::Begin(name());

    const ImPlotAxisFlags x_flags = ImPlotAxisFlags_Lock;
    const ImPlotAxisFlags y_flags = ImPlotAxisFlags_Lock;

    if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
        _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
    }

    // Time-span (history) control, expressed in SECONDS. Under the hood it
    // still sets frames-per-row (how many FFT frames are peak-held into one
    // row, in [1, 256]) -- one row spans fpr * n_fft / sps seconds and the
    // ring holds _tall rows, so the total on-screen history is
    // fpr * n_fft * tall / sps. The committed slider value is quantized back
    // onto the integer frames-per-row, so the grab snaps to representable
    // depths. Bounds and the displayed value are recomputed every frame from
    // the CURRENT frames-per-row and sample rate (both can change live).
    ImGui::SameLine();
    int fpr = static_cast<int>(_frames_per_row.load());
    if (_sps > 0) {
        const float sec_per_step = static_cast<float>(_tall)
                                 * static_cast<float>(_n_fft_samples)
                                 / static_cast<float>(_sps);
        float shown = static_cast<float>(fpr) * sec_per_step;
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("History", &shown, sec_per_step,
                               256.0f * sec_per_step, "%.1f s",
                               ImGuiSliderFlags_Logarithmic)) {
            long n = std::lround(shown / sec_per_step);
            if (n < 1)   n = 1;
            if (n > 256) n = 256;
            fpr = static_cast<int>(n);
            _frames_per_row.store(static_cast<size_t>(fpr));
        }
    } else {
        // Sample rate unknown: seconds are meaningless, edit frames/row raw.
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderInt("History (frames/row)", &fpr, 1, 256, "%d",
                             ImGuiSliderFlags_Logarithmic)) {
            if (fpr < 1) fpr = 1;
            _frames_per_row.store(static_cast<size_t>(fpr));
        }
    }

    // Measurement-grid toggle, sat next to History. The line spacing (time in
    // ms, frequency in MHz) is set from the control panel; here we only flip it
    // on/off. The grid is drawn on TOP of the waterfall image below.
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &_show_grid);

    // Seconds per waterfall row at the CURRENT frames/row setting. Rows already
    // in the ring may have been produced under a different setting, so the time
    // axis is exact only for rows written since the last change -- the same
    // caveat as the History slider above. Falls back to row units if the
    // sample rate is unknown.
    const double row_dt = (_sps > 0)
        ? static_cast<double>(fpr) * static_cast<double>(_n_fft_samples)
              / static_cast<double>(_sps)
        : 1.0;

    // While PAUSED, freeze the row_dt the IMAGE is drawn with (captured at the
    // instant of pausing) while the Y AXIS still tracks the LIVE History value.
    // The image is thus pinned in true seconds and the viewport zooms around
    // it: widening History (bigger axis span) shrinks the frozen capture into
    // the top of a longer window (older time below is empty -- we have no data
    // there), and shrinking zooms into the most recent slice. Because both the
    // image and the measurement grid live in the same data-seconds space while
    // only the axis zooms, the grid stays locked to the data and scales with
    // it. Live, image and axis share row_dt so the picture just fills the plot.
    const bool paused = _gui_pause.load(std::memory_order_acquire);
    if (paused && !_was_paused) _paused_row_dt = row_dt;   // capture on entry
    _was_paused = paused;
    const double img_row_dt = paused ? _paused_row_dt : row_dt;

    // ---- Waterfall texture maintenance (all GL strictly on this thread) ----

    // 256-entry colormap LUT, built once. ImPlot::SampleColormap() goes
    // through the exact same ColormapData::LerpTable() the old PlotHeatmap
    // per-cell colorizer used, so colors match up to the 1/256 quantization.
    // ImGui packs the ImU32 as R,G,B,A bytes in memory, which is precisely
    // what GL_RGBA + GL_UNSIGNED_BYTE consumes.
    if (!_lut_built) {
        for (int j = 0; j < 256; ++j) {
            ImVec4 c = ImPlot::SampleColormap(static_cast<float>(j) / 255.0f,
                                              ImPlotColormap_Plasma);
            _lut[j] = ImGui::ColorConvertFloat4ToU32(c);
        }
        _lut_built = true;
    }

    // Lazy texture creation (the GUI thread owns GL; the constructor may run
    // before a context exists).
    if (_tex[0] == 0) {
        GLint prev_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
        for (size_t i = 0; i < _num_inputs; ++i) {
            glGenTextures(1, &_tex[i]);
            glBindTexture(GL_TEXTURE_2D, _tex[i]);
            // NEAREST matches PlotHeatmap's hard per-cell rectangles.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         static_cast<GLsizei>(_n_fft_samples),
                         static_cast<GLsizei>(_tall),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
        _tex_full_dirty = true;   // populate the fresh (undefined) texels
    }

    // Plan + copy-out under the ring mutex; colorize/upload AFTER unlocking
    // (never hold the DSP-facing lock across GL calls). On the steady path
    // only the rows that arrived since the last frame are touched.
    bool   any_work = false;   // anything to colorize/upload this frame?
    size_t up_start = 0;       // first incremental ring row
    size_t up_count = 0;       // number of incremental rows (0 on full path)
    {
        std::lock_guard<std::mutex> lock(_spectrogram_mutex);
        const size_t unseen = _row_gen - _row_gen_seen;
        if (unseen != 0 || _tex_full_dirty) {
            _row_gen_seen   = _row_gen;
            any_work        = true;
            _tex_ring_pos   = _ring_pos;
            _tex_ring_count = _ring_count;

            // Full recolor+re-upload only on: texture (re)create, ring clear
            // (set_sample_rate), color-scale rebake (below), or when more
            // rows arrived than the ring holds.
            const bool full_all = _tex_full_dirty || unseen >= _tall;
            _tex_full_dirty = false;

            // Refresh per-row min/max stats (scale decisions are O(tall)).
            if (full_all) {
                for (size_t i = 0; i < _num_inputs; ++i) {
                    for (size_t r = 0; r < _tall; ++r) {
                        if (r < _ring_count) {
                            const float* src = _spectrograms[i] + r * _n_fft_samples;
                            float mn = src[0], mx = src[0];
                            for (size_t x = 1; x < _n_fft_samples; ++x) {
                                mn = std::min(mn, src[x]);
                                mx = std::max(mx, src[x]);
                            }
                            _row_min[i][r] = mn;
                            _row_max[i][r] = mx;
                        } else {
                            _row_min[i][r] = DB_FLOOR;
                            _row_max[i][r] = DB_FLOOR;
                        }
                    }
                }
            } else {
                up_count = unseen;
                up_start = (_ring_pos + _tall - unseen) % _tall;
                for (size_t j = 0; j < up_count; ++j) {
                    const size_t r = (up_start + j) % _tall;
                    for (size_t i = 0; i < _num_inputs; ++i) {
                        const float* src = _spectrograms[i] + r * _n_fft_samples;
                        float mn = src[0], mx = src[0];
                        for (size_t x = 1; x < _n_fft_samples; ++x) {
                            mn = std::min(mn, src[x]);
                            mx = std::max(mx, src[x]);
                        }
                        _row_min[i][r] = mn;
                        _row_max[i][r] = mx;
                    }
                }
            }

            // Color scale: PlotHeatmap auto-scaled to the data min/max every
            // frame (scale_min == scale_max == 0). Baked texels cannot follow
            // a per-frame drift without a full re-upload each frame, so the
            // scale gets a MARGIN_DB pad and hysteresis: rebake (full recolor)
            // only when data leaves the baked range or the range shrank by
            // more than the slack. Unwritten rows count as DB_FLOOR, exactly
            // like the old padded display buffer did.
            constexpr float MARGIN_DB = 3.0f;
            constexpr float SHRINK_SLACK_DB = 6.0f;
            for (size_t i = 0; i < _num_inputs; ++i) {
                float gmin = FLT_MAX, gmax = -FLT_MAX;
                for (size_t r = 0; r < _tall; ++r) {
                    gmin = std::min(gmin, _row_min[i][r]);
                    gmax = std::max(gmax, _row_max[i][r]);
                }
                bool rescale;
                float new_min, new_max;
                if (gmin == gmax) {
                    // All data equal (e.g. empty ring): PlotHeatmap drew a
                    // solid rect in colormap color 0; flat scale does the same.
                    rescale = !(_scale_min[i] == _scale_max[i] && _scale_min[i] == gmin);
                    new_min = new_max = gmin;
                } else {
                    const bool was_flat = _scale_min[i] >= _scale_max[i];
                    rescale = was_flat
                           || gmax > _scale_max[i]
                           || gmin < _scale_min[i]
                           || (_scale_max[i] - _scale_min[i])
                                > (gmax - gmin) + 2.0f * MARGIN_DB + SHRINK_SLACK_DB;
                    new_min = gmin - MARGIN_DB;
                    new_max = gmax + MARGIN_DB;
                }
                if (rescale) {
                    _scale_min[i] = new_min;
                    _scale_max[i] = new_max;
                }
                _needs_full[i] = full_all || rescale;
            }

            // Copy the needed dB rows out; colorization happens after unlock.
            // Incremental rows keep their ring offsets inside _stage so the
            // colorizer can index both the same way.
            for (size_t i = 0; i < _num_inputs; ++i) {
                if (_needs_full[i]) {
                    memcpy(_stage[i], _spectrograms[i],
                           _tall * _n_fft_samples * sizeof(float));
                } else {
                    for (size_t j = 0; j < up_count; ++j) {
                        const size_t r = (up_start + j) % _tall;
                        memcpy(_stage[i] + r * _n_fft_samples,
                               _spectrograms[i] + r * _n_fft_samples,
                               _n_fft_samples * sizeof(float));
                    }
                }
            }
        }
    }

    if (any_work) {
        // Colorize staged dB rows through the LUT and upload in place. A
        // contiguous ring span [row0, row0+nrows) maps to the same rows of
        // the texture (texture row == ring row).
        auto colorize_upload = [&](size_t i, size_t row0, size_t nrows) {
            const float smin = _scale_min[i];
            const float smax = _scale_max[i];
            if (smin >= smax) {
                std::fill_n(_pixels, nrows * _n_fft_samples, _lut[0]);
            } else {
                const float inv = 255.0f / (smax - smin);
                ImU32* dst = _pixels;
                for (size_t r = row0; r < row0 + nrows; ++r) {
                    const float* src = _stage[i] + r * _n_fft_samples;
                    for (size_t x = 0; x < _n_fft_samples; ++x) {
                        float t = (src[x] - smin) * inv + 0.5f;
                        int idx = static_cast<int>(t);
                        if (idx < 0)   idx = 0;
                        if (idx > 255) idx = 255;
                        *dst++ = _lut[idx];
                    }
                }
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            0, static_cast<GLint>(row0),
                            static_cast<GLsizei>(_n_fft_samples),
                            static_cast<GLsizei>(nrows),
                            GL_RGBA, GL_UNSIGNED_BYTE, _pixels);
        };

        GLint prev_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);  // rows in _pixels are packed
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);   // RGBA8 rows are 4-byte aligned
        for (size_t i = 0; i < _num_inputs; ++i) {
            glBindTexture(GL_TEXTURE_2D, _tex[i]);
            if (_needs_full[i]) {
                colorize_upload(i, 0, _tall);
            } else {
                // New rows are contiguous in ring space except for at most one
                // wrap at the end of the buffer: at most two glTexSubImage2D.
                const size_t first = std::min(up_count, _tall - up_start);
                if (first > 0)             colorize_upload(i, up_start, first);
                if (up_count - first > 0)  colorize_upload(i, 0, up_count - first);
            }
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
    }

    // ---- Draw: the chronological ring as two quads split at the seam ----
    // Ring row r has age k = (ring_pos - 1 - r) mod tall (k = 0 newest) and
    // occupies y in [k, k+1] * row_dt -- identical to what PlotHeatmap drew
    // from the reordered buffer (its row 0 sat against bounds_max.y = 0).
    //   Rows [0, ring_pos)    = ages ring_pos-1 .. 0 -> y in [0, ring_pos*row_dt],
    //                           texture v = 0 at the TOP of that span;
    //   Rows [ring_pos, tall) = ages tall-1 .. ring_pos
    //                           -> y in [ring_pos*row_dt, tall*row_dt],
    //                           texture v = ring_pos/tall at the TOP.
    // PlotImage anchors uv0 at (bounds_min.x, bounds_max.y), i.e. the top-left
    // in plot coords, which is exactly that mapping. Unwritten rows hold
    // DB_FLOOR texels, so they render as the same floor-colored padding the
    // old display buffer had.
    const double x_min = -static_cast<double>(_sps) / 2.0;
    const double x_max =  static_cast<double>(_sps) / 2.0;
    const float  seam_v = static_cast<float>(_tex_ring_pos) / static_cast<float>(_tall);

    for (size_t i = 0; i < _num_inputs; ++i) {
        if (ImPlot::BeginPlot(_signal_labels[i].c_str(), ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Frequency (Hz)", "Time (s)", x_flags, y_flags);
            // Default cond is Once, which would pin the axis to the span at
            // first render; after set_sample_rate() a one-shot Always re-fits
            // it to the new span (safe: the axis is Lock'ed, so Always cannot
            // fight user pan/zoom).
            ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max,
                                    _axis_refit ? ImPlotCond_Always : ImPlotCond_Once);
            // Elapsed time in seconds. frames/row can change live, which
            // rescales the axis, so this must be ImPlotCond_Always -- safe
            // here because the axis is Lock'ed, so Always cannot fight user
            // pan/zoom.
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(_tall) * row_dt, 0.0,
                                    ImPlotCond_Always);

            if (_tex[i] != 0) {
                // C-style: ImTextureID is ImU64 on current ImGui but has been
                // void* historically; both accept an integer round-trip.
                const ImTextureID tid = (ImTextureID)(intptr_t)_tex[i];
                const std::string label = "##" + _signal_labels[i];
                if (_tex_ring_pos > 0) {
                    ImPlot::PlotImage(label.c_str(), tid,
                        ImPlotPoint(x_min, 0.0),
                        ImPlotPoint(x_max, static_cast<double>(_tex_ring_pos) * img_row_dt),
                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, seam_v));
                }
                if (_tex_ring_pos < _tall) {
                    const std::string label2 = label + "wrap";
                    ImPlot::PlotImage(label2.c_str(), tid,
                        ImPlotPoint(x_min, static_cast<double>(_tex_ring_pos) * img_row_dt),
                        ImPlotPoint(x_max, static_cast<double>(_tall) * img_row_dt),
                        ImVec2(0.0f, seam_v), ImVec2(1.0f, 1.0f));
                }
            }

            // Measurement grid, drawn on top of the (opaque) waterfall image.
            // Vertical lines mark frequency (spaced grid_freq MHz, anchored at
            // DC = 0), horizontal lines mark time (spaced grid_time ms, anchored
            // at t = 0 which is the top of the plot). PlotToPixels maps plot
            // coords to screen; the per-axis span/count guard skips a
            // pathologically dense grid (tiny spacing) so we never spam
            // thousands of AddLine calls.
            if (_show_grid) {
                ImDrawList* dl = ImPlot::GetPlotDrawList();
                const ImPlotRect lim = ImPlot::GetPlotLimits();
                const ImU32 grid_col = IM_COL32(255, 255, 255, 70);
                constexpr float thickness = 1.0f;
                constexpr double MAX_LINES = 2000.0;   // per-axis sanity cap
                ImPlot::PushPlotClipRect();

                const double f_step = static_cast<double>(_grid_freq_mhz) * 1e6;
                if (f_step > 0.0 && (lim.X.Max - lim.X.Min) / f_step < MAX_LINES) {
                    for (double fx = 0.0; fx <= lim.X.Max; fx += f_step) {
                        dl->AddLine(ImPlot::PlotToPixels(fx, lim.Y.Min),
                                    ImPlot::PlotToPixels(fx, lim.Y.Max),
                                    grid_col, thickness);
                    }
                    for (double fx = -f_step; fx >= lim.X.Min; fx -= f_step) {
                        dl->AddLine(ImPlot::PlotToPixels(fx, lim.Y.Min),
                                    ImPlot::PlotToPixels(fx, lim.Y.Max),
                                    grid_col, thickness);
                    }
                }

                const double t_step = static_cast<double>(_grid_time_ms) / 1e3;
                const double t_max = std::max(lim.Y.Min, lim.Y.Max);
                if (t_step > 0.0 && t_max / t_step < MAX_LINES) {
                    for (double ty = 0.0; ty <= t_max; ty += t_step) {
                        dl->AddLine(ImPlot::PlotToPixels(lim.X.Min, ty),
                                    ImPlot::PlotToPixels(lim.X.Max, ty),
                                    grid_col, thickness);
                    }
                }
                ImPlot::PopPlotClipRect();
            }
            ImPlot::EndPlot();
        }
    }
    _axis_refit = false;   // one-shot consumed (all input plots saw it)
    ImGui::End();
}

// GUI-THREAD-ONLY (see header). The ring clear is safe against the DSP
// thread because the row flush in procedure() runs under _spectrogram_mutex.
// The peak-hold accumulator (_accum_count) is deliberately NOT reset: its
// reset on the paused path in procedure() is not mutex-guarded, so touching
// it here would race. Consequence: at most one transitional row may mix
// frames recorded at the old and new rates.
void PlotCSpectrogramBlock::set_sample_rate(size_t sps) {
    _sps = sps;
    for (size_t i = 0; i < _n_fft_samples; ++i) {
        _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(_n_fft_samples))) - (_sps / 2.0f);
    }
    {
        std::lock_guard<std::mutex> lock(_spectrogram_mutex);
        _ring_pos   = 0;
        _ring_count = 0;
        for (size_t i = 0; i < _num_inputs; ++i) {
            std::fill_n(_spectrograms[i], _tall * _n_fft_samples, DB_FLOOR);
            // Row stats must match the cleared ring so the next scale
            // decision doesn't see stale peaks (GUI-thread data, but cheap to
            // reset here under the same lock as the ring).
            std::fill_n(_row_min[i], _tall, DB_FLOOR);
            std::fill_n(_row_max[i], _tall, DB_FLOOR);
        }
        // Bump the row generation and force a full texture recolor+upload so
        // the next render() rebuilds from the (now empty) ring instead of
        // keeping the stale image.
        ++_row_gen;
        _tex_full_dirty = true;
    }
    _axis_refit = true;   // re-fit the X axis to the new span on next render()
}

// GUI-thread-only; see header. render() no longer keeps a reordered display
// copy, so the newest-first image is assembled here on demand, straight from
// the chronological ring under _spectrogram_mutex (snapshots are rare; this
// O(tall * n_fft) copy is off the per-frame render path).
bool PlotCSpectrogramBlock::export_display(size_t channel, std::vector<float>& data,
                                           size_t& rows, size_t& cols,
                                           size_t& frames_per_row_out, size_t& sps_out) const {
    if (channel >= _num_inputs) return false;
    std::lock_guard<std::mutex> lock(_spectrogram_mutex);
    if (_ring_count == 0) return false;
    data.resize(_tall * _n_fft_samples);
    for (size_t k = 0; k < _tall; ++k) {
        float* dst = data.data() + k * _n_fft_samples;
        if (k < _ring_count) {
            size_t src_row = (_ring_pos + _tall - 1 - k) % _tall;
            memcpy(dst, _spectrograms[channel] + src_row * _n_fft_samples,
                   _n_fft_samples * sizeof(float));
        } else {
            std::fill_n(dst, _n_fft_samples, DB_FLOOR);
        }
    }
    rows               = _tall;
    cols               = _n_fft_samples;
    frames_per_row_out = _frames_per_row.load(std::memory_order_relaxed);
    sps_out            = _sps;
    return true;
}

void PlotCSpectrogramBlock::set_initial_window(float x, float y, float w, float h) {
    _initial_window_position = ImVec2(x, y);
    _initial_window_size = ImVec2(w, h);
}
