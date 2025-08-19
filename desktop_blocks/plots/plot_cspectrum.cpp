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

    // Calculate buffer size with better DBF compatibility
    // Use larger multiplier for small FFT sizes to ensure efficient DBF operation
    size_t min_buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
    
    // For small FFT sizes, increase the multiplier to ensure we have enough buffer
    // This improves compatibility with DBF-using upstream blocks
    size_t effective_multiplier = BUFFER_SIZE_MULTIPLIER;
    if (_n_fft_samples < min_buffer_size) {
        // Increase multiplier to ensure buffer is at least 2x the minimum DBF size
        // This provides better buffering for high-throughput DBF sources
        effective_multiplier = std::max(
            BUFFER_SIZE_MULTIPLIER,
            (2 * min_buffer_size + _n_fft_samples - 1) / _n_fft_samples
        );
    }
    
    _buffer_size = effective_multiplier * _n_fft_samples;
    
    // Final check to ensure minimum size
    if (_buffer_size < min_buffer_size) {
        _buffer_size = min_buffer_size;
    }

    // Input channels with proper buffer size
    in = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<std::complex<float>>(_buffer_size);
    }

    // Plot ring buffers with same size as input channels
    _signal_channels = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&_signal_channels[i]) cler::Channel<std::complex<float>>(_buffer_size);
    }

    // Snapshot buffers - only need FFT size, not full buffer size
    _snapshot_buffers = new std::complex<float>*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _snapshot_buffers[i] = new std::complex<float>[_n_fft_samples];
    }

    _tmp_buffer = new std::complex<float>[_buffer_size];

    _liquid_inout = new std::complex<float>[_n_fft_samples];
    _tmp_mag_buffer = new float[_n_fft_samples];
    _freq_bins = new float[_n_fft_samples];
    for (size_t i = 0; i < _n_fft_samples; ++i) {
        _freq_bins[i] = (_sps * (static_cast<float>(i) / static_cast<float>(_n_fft_samples)))
                      - (_sps / 2.0f);
    }
    
    // Allocate averaged spectrum buffers for each input
    _spectrum_avg = new float*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _spectrum_avg[i] = new float[_n_fft_samples];
        std::memset(_spectrum_avg[i], 0, _n_fft_samples * sizeof(float));
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
    
    // Cleanup averaged spectrum buffers
    if (_spectrum_avg) {
        for (size_t i = 0; i < _num_inputs; ++i) {
            delete[] _spectrum_avg[i];
        }
        delete[] _spectrum_avg;
    }
    
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
        available = _signal_channels[0].size();
        for (size_t i = 1; i < _num_inputs; ++i) {
            size_t a = _signal_channels[i].size();
            if (a != available) {
                available = std::min(available, a);
            }
        }

        _snapshot_ready_size = available;

        // Only snapshot if enough samples
        if (available >= _n_fft_samples) {
            for (size_t i = 0; i < _num_inputs; ++i) {
                auto [ptr, size] = _signal_channels[i].read_dbf();
                // Copy only the last _n_fft_samples samples for FFT
                memcpy(_snapshot_buffers[i], 
                       ptr + size - _n_fft_samples, 
                       _n_fft_samples * sizeof(std::complex<float>));
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
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);  // Small fixed width slider
    ImGui::SliderFloat("##avg", &_avg_alpha, 0.0f, 1.0f, "alpha:%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Averaging: 0=frozen, 0.3=heavy, 0.7=light, 1=none");
    }

    if (ImPlot::BeginPlot(name())) {
        ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");

        for (size_t i = 0; i < _num_inputs; ++i) {
            memcpy(_liquid_inout,
                   _snapshot_buffers[i],
                   _n_fft_samples * sizeof(std::complex<float>));

            float coherent_gain = 0.0f;
            for (size_t n = 0; n < _n_fft_samples; ++n) {
                float w = spectral_window_function(_window_type, n / static_cast<float>(_n_fft_samples - 1));
                coherent_gain += w;
                _liquid_inout[n] *= w;
            }
            coherent_gain /= static_cast<float>(_n_fft_samples);

            fft_execute(_fftplan);
            fft_shift(_liquid_inout, _n_fft_samples);

            float scale = static_cast<float>(_n_fft_samples) * coherent_gain;
            float scale2 = scale * scale;

            for (size_t j = 0; j < _n_fft_samples; ++j) {
                float re = _liquid_inout[j].real();
                float im = _liquid_inout[j].imag();
                float power = (re * re + im * im) / scale2;
                _tmp_mag_buffer[j] = 10.0f * log10f(power + 1e-20f);
            }
            
            // Apply exponential averaging
            if (_first_spectrum) {
                // First frame: initialize with current values
                std::memcpy(_spectrum_avg[i], _tmp_mag_buffer, _n_fft_samples * sizeof(float));
                if (i == _num_inputs - 1) {
                    _first_spectrum = false;
                }
            } else {
                // Exponential moving average: new = alpha * current + (1 - alpha) * old
                for (size_t j = 0; j < _n_fft_samples; ++j) {
                    _spectrum_avg[i][j] = _avg_alpha * _tmp_mag_buffer[j] + 
                                          (1.0f - _avg_alpha) * _spectrum_avg[i][j];
                }
            }

            ImPlot::PlotLine(_signal_labels[i].c_str(),
                             _freq_bins,
                             _spectrum_avg[i],  // Plot averaged spectrum instead
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
