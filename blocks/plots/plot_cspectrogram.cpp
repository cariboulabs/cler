#include "plot_cspectrogram.hpp"
#include "implot.h"

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
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<std::complex<float>>(BUFFER_SIZE_MULTIPLIER * _n_fft_samples);
    }

    _liquid_inout = new std::complex<float>[_n_fft_samples];
    _tmp_y_buffer = new std::complex<float>[_n_fft_samples];
    _tmp_mag_buffer = new float[_n_fft_samples];

    _spectrograms = new float*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _spectrograms[i] = new float[_tall * _n_fft_samples]();
        std::fill_n(_spectrograms[i], _tall * _n_fft_samples, -147.0f);
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
    }
    delete[] _spectrograms;

    delete[] _freq_bins;
    fft_destroy_plan(_fftplan);
}

cler::Result<cler::Empty, cler::Error> PlotCSpectrogramBlock::procedure() {
    if (_gui_pause.load(std::memory_order_acquire)) {
        return cler::Empty{};
    }

    size_t available = in[0].size();
    for (size_t i = 1; i < _num_inputs; ++i) {
        if (in[i].size() < available) available = in[i].size();
    }
    if (available < _n_fft_samples) {
        return cler::Error::NotEnoughSamples;
    }

    std::lock_guard<std::mutex> lock(_spectrogram_mutex);

    for (size_t i = 0; i < _num_inputs; ++i) {
        in[i].readN(_tmp_y_buffer, _n_fft_samples);
        memcpy(_liquid_inout, _tmp_y_buffer, _n_fft_samples * sizeof(std::complex<float>));

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

        // Shift up and insert new row
        memmove(
            _spectrograms[i] + _n_fft_samples,
            _spectrograms[i],
            (_tall - 1) * _n_fft_samples * sizeof(float)
        );
        memcpy(
            _spectrograms[i],
            _tmp_mag_buffer,
            _n_fft_samples * sizeof(float)
        );
    }

    return cler::Empty{};
}

void PlotCSpectrogramBlock::render() {
    ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    ImGui::Begin(name());

    const ImPlotAxisFlags x_flags = ImPlotAxisFlags_Lock;
    const ImPlotAxisFlags y_flags = ImPlotAxisFlags_Lock;

    std::lock_guard<std::mutex> lock(_spectrogram_mutex);

    if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
        _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
    }

    for (size_t i = 0; i < _num_inputs; ++i) {
        if (ImPlot::BeginPlot(_signal_labels[i].c_str())) {
            ImPlot::SetupAxes("Frequency (Hz)", "Time (frames)", x_flags, y_flags);
            ImPlot::SetupAxisLimits(ImAxis_X1, -static_cast<double>(_sps)/2.0, static_cast<double>(_sps)/2.0);
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(_tall), 0.0);
            ImPlot::PushColormap(ImPlotColormap_Plasma);

            std::string label = "##" + std::string(_signal_labels[i]);
            ImPlot::PlotHeatmap(
                label.c_str(),
                _spectrograms[i],
                _tall,
                _n_fft_samples,
                0.0, 0.0,
                nullptr,
                ImPlotPoint(-static_cast<double>(_sps)/2.0, static_cast<double>(_tall)),
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
    _has_initial_window_position = true;
}
