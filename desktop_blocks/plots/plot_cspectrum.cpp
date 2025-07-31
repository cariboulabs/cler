#include "plot_cspectrum.hpp"
#include "implot.h"

#include <cstring>
#include <stdexcept>
#include <cassert>
#include <cmath>

PlotCSpectrumBlock::PlotCSpectrumBlock(const char* name,
    const std::vector<std::string>& signal_labels,
    size_t sps,
    size_t n_fft_samples,
    SpectralWindow window_type)
    : BlockBase(name),
      _num_inputs(signal_labels.size()),
      _signal_labels(signal_labels),
      _sps(sps),
      _n_fft_samples(n_fft_samples),
      _window_type(window_type)
{
    if (_num_inputs < 1) {
        throw std::invalid_argument("PlotCSpectrumBlock requires at least one input channel");
    }
    if (n_fft_samples <= 2 || n_fft_samples % 2 != 0) {
        throw std::invalid_argument("FFT size must be > 2 and even");
    }

    _buffer_size = BUFFER_SIZE_MULTIPLIER * _n_fft_samples;

    // Input channels
    in = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<std::complex<float>>(_buffer_size);
    }

    // Plot ring buffers
    _signal_channels = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&_signal_channels[i]) cler::Channel<std::complex<float>>(_buffer_size);
    }

    // Snapshot buffers
    _snapshot_buffers = new std::complex<float>*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _snapshot_buffers[i] = new std::complex<float>[_buffer_size];
    }

    _tmp_buffer = new std::complex<float>[_buffer_size];

    _liquid_inout = new std::complex<float>[_n_fft_samples];
    _tmp_mag_buffer = new float[_n_fft_samples];
    _freq_bins = new float[_n_fft_samples];
    for (size_t i = 0; i < _n_fft_samples; ++i) {
        _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(_n_fft_samples)))
                      - (_sps / 2.0f);
    }

    _fftplan = fft_create_plan(_n_fft_samples,
        reinterpret_cast<liquid_float_complex*>(_liquid_inout),
        reinterpret_cast<liquid_float_complex*>(_liquid_inout),
        LIQUID_FFT_FORWARD, 0);
}

PlotCSpectrumBlock::~PlotCSpectrumBlock() {
    using ComplexChannel = cler::Channel<std::complex<float>>;
    for (size_t i = 0; i < _num_inputs; ++i) {
        in[i].~ComplexChannel();
        _signal_channels[i].~ComplexChannel();
    }
    ::operator delete[](in);
    ::operator delete[](_signal_channels);

    for (size_t i = 0; i < _num_inputs; ++i) {
        delete[] _snapshot_buffers[i];
    }
    delete[] _snapshot_buffers;

    delete[] _tmp_buffer;
    delete[] _liquid_inout;
    delete[] _tmp_mag_buffer;
    delete[] _freq_bins;
    fft_destroy_plan(_fftplan);
}

cler::Result<cler::Empty, cler::Error> PlotCSpectrumBlock::procedure() {
    if (_gui_pause.load(std::memory_order_acquire)) {
        return cler::Empty{};
    }

    size_t work_size = in[0].size();
    for (size_t i = 1; i < _num_inputs; ++i) {
        if (in[i].size() < work_size) {
            work_size = in[i].size();
        }
    }
    if (work_size == 0) {
        return cler::Error::NotEnoughSamples;
    }

    for (size_t i = 0; i < _num_inputs; ++i) {
        size_t commit_size = (_signal_channels[i].size() + work_size > _buffer_size)
            ? (_signal_channels[i].size() + work_size - _buffer_size) : 0;

        // Try zero-copy path first
        auto [read_ptr, read_size] = in[i].read_dbf();
        if (read_ptr && read_size >= work_size) {
            // Try to write directly to internal buffer
            auto [write_ptr, write_size] = _signal_channels[i].write_dbf();
            if (write_ptr && write_size >= work_size) {
                // FAST PATH: Direct copy from input to internal buffer
                _signal_channels[i].commit_read(commit_size);
                std::memcpy(write_ptr, read_ptr, work_size * sizeof(std::complex<float>));
                _signal_channels[i].commit_write(work_size);
                in[i].commit_read(work_size);
            } else {
                // Input has dbf but output doesn't
                _signal_channels[i].commit_read(commit_size);
                _signal_channels[i].writeN(read_ptr, work_size);
                in[i].commit_read(work_size);
            }
        } else {
            // Fall back to standard approach
            in[i].readN(_tmp_buffer, work_size);
            _signal_channels[i].commit_read(commit_size);
            _signal_channels[i].writeN(_tmp_buffer, work_size);
        }
    }

    _samples_counter += work_size;
    return cler::Empty{};
}


void PlotCSpectrumBlock::render() {
    // Take snapshot inside render, protected by mutex
    size_t available = 0;

    if (_snapshot_mutex.try_lock()) {

        const std::complex<float>* ptr1; const std::complex<float>* ptr2;
        size_t size1, size2;

        available = _signal_channels[0].peek_read(ptr1, size1, ptr2, size2);
        for (size_t i = 1; i < _num_inputs; ++i) {
            size_t a = _signal_channels[i].peek_read(ptr1, size1, ptr2, size2);
            if (a != available) {
                fprintf(stderr, "Channel %zu: a = %zu, available = %zu\n", i, a, available);
                available = std::min(available, a);
            }
        }

        _snapshot_ready_size = available;

        // Only snapshot if enough samples
        if (available >= _n_fft_samples) {
            for (size_t i = 0; i < _num_inputs; ++i) {
                size_t dummy1, dummy2;
                const std::complex<float>* p1;
                const std::complex<float>* p2;

                _signal_channels[i].peek_read(p1, dummy1, p2, dummy2);
                memcpy(_snapshot_buffers[i], p1, dummy1 * sizeof(std::complex<float>));
                memcpy(_snapshot_buffers[i] + dummy1, p2, dummy2 * sizeof(std::complex<float>));
            }
        }
         _snapshot_mutex.unlock();
    }

    if (_snapshot_ready_size < _n_fft_samples) {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());
        ImGui::Text("Not enough samples for FFT. Need at least %zu, got %zu.",
                    _n_fft_samples, _snapshot_ready_size);
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    ImGui::Begin(name());

    if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
        _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
    }

    if (ImPlot::BeginPlot(name())) {
        ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");

        for (size_t i = 0; i < _num_inputs; ++i) {
            memcpy(_liquid_inout,
                   &_snapshot_buffers[i][_snapshot_ready_size - _n_fft_samples],
                   _n_fft_samples * sizeof(std::complex<float>));

            float coherent_gain = 0.0f;
            for (size_t n = 0; n < _n_fft_samples; ++n) {
                float w = spectral_window_function(_window_type, n / static_cast<float>(_n_fft_samples - 1));
                coherent_gain += w;
                _liquid_inout[n] *= w * ((n % 2 == 0) ? 1.0f : -1.0f);
            }
            coherent_gain /= static_cast<float>(_n_fft_samples);

            fft_execute(_fftplan);

            float scale = static_cast<float>(_n_fft_samples) * coherent_gain;
            float scale2 = scale * scale;

            for (size_t j = 0; j < _n_fft_samples; ++j) {
                float re = _liquid_inout[j].real();
                float im = _liquid_inout[j].imag();
                float power = (re * re + im * im) / scale2;
                _tmp_mag_buffer[j] = 10.0f * log10f(power + 1e-20f);
            }

            ImPlot::PlotLine(_signal_labels[i].c_str(),
                             _freq_bins,
                             _tmp_mag_buffer,
                             static_cast<int>(_n_fft_samples));
        }
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void PlotCSpectrumBlock::set_initial_window(float x, float y, float w, float h) {
    _initial_window_position = ImVec2(x, y);
    _initial_window_size = ImVec2(w, h);
}
