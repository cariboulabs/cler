#include "plot_cspectrum.hpp"
#include "implot.h"
#include "cler_addons.hpp"

PlotCSpectrumBlock::PlotCSpectrumBlock(std::string name,
    const std::vector<std::string> signal_labels,
    const size_t sps,
    const size_t n_fft_samples,
    const SpectralWindow window_type)
    : BlockBase(std::move(name)),
      _num_inputs(signal_labels.size()),
      _signal_labels(std::move(signal_labels)),
      _sps(sps),
      _n_fft_samples(n_fft_samples),
      _window_type(window_type)
{
    if (_num_inputs < 1) {
        throw std::invalid_argument("PlotCSpectrumBlock requires at least one input channel");
    }
    if (_n_fft_samples <= 2) {
        throw std::invalid_argument("FFT size must be greater than two.");
    }
    if (_n_fft_samples % 2 != 0) {
        throw std::invalid_argument("FFT size must be even.");
    }

    // Allocate input channels
    in = static_cast<cler::Channel<std::complex<float>>*>(
        ::operator new[](_num_inputs * sizeof(cler::Channel<std::complex<float>>))
    );
    for (size_t i = 0; i < _num_inputs; ++i) {
        new (&in[i]) cler::Channel<std::complex<float>>(_n_fft_samples * BUFFER_SIZE_MULTIPLIER);
    }

    _liquid_inout = new std::complex<float>[_n_fft_samples];
    _tmp_mag_buffer = new float[_n_fft_samples];
    _buffers0 = new std::complex<float>*[_num_inputs];
    _buffers1 = new std::complex<float>*[_num_inputs];
    for (size_t i = 0; i < _num_inputs; ++i) {
        _buffers0[i] = new std::complex<float>[_n_fft_samples]();
        _buffers1[i] = new std::complex<float>[_n_fft_samples]();
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

PlotCSpectrumBlock::~PlotCSpectrumBlock() {
    using ComplexChannel = cler::Channel<std::complex<float>>;
    for (size_t i = 0; i < _num_inputs; ++i) {
        in[i].~ComplexChannel();
    }
    ::operator delete[](in);

    delete[] _liquid_inout;
    delete[] _tmp_mag_buffer;

    for (size_t i = 0; i < _num_inputs; ++i) {
        delete[] _buffers0[i];
        delete[] _buffers1[i];
    }
    delete[] _buffers0;
    delete[] _buffers1;

    delete[] _freq_bins;
    fft_destroy_plan(_fftplan);
}

cler::Result<cler::Empty, cler::Error> PlotCSpectrumBlock::procedure() {
    size_t available = in[0].size();
    for (size_t i = 1; i < _num_inputs; ++i) {
        if (in[i].size() < available) {
            available = in[i].size();
        }
    }
    if (available < _n_fft_samples) {
        return cler::Error::NotEnoughSamples;
    }

    uint8_t load_buffer = (_show_buffer.load(std::memory_order_relaxed) + 1) % 2;
    for (size_t i = 0; i < _num_inputs; ++i) {
        if (available > 2 * _n_fft_samples) {in[i].commit_read(available - _n_fft_samples);}
        if (load_buffer == 0) {
            in[i].readN(_buffers0[i], _n_fft_samples);
        } else {
            in[i].readN(_buffers1[i], _n_fft_samples);
        }
    }
    _show_buffer.store(load_buffer, std::memory_order_release);

    return cler::Empty{};
}

void PlotCSpectrumBlock::render() {
    ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
    ImGui::Begin(name().c_str());

    if (ImPlot::BeginPlot(name().c_str())) {
        ImPlot::SetupAxes("Frequency [Hz]", "Magnitude [dB]");

        uint8_t show_buffer = _show_buffer.load(std::memory_order_acquire);
        for (size_t i = 0; i < _num_inputs; ++i) {
            if (show_buffer == 0) {
                memcpy(_liquid_inout, _buffers0[i], _n_fft_samples * sizeof(std::complex<float>));
            } else {
                memcpy(_liquid_inout, _buffers1[i], _n_fft_samples * sizeof(std::complex<float>));
            }

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
                             _n_fft_samples);
        }
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void PlotCSpectrumBlock::set_initial_window(float x, float y, float w, float h) {
    _initial_window_position = ImVec2(x, y);
    _initial_window_size = ImVec2(w, h);
    _has_initial_window_position = true;
}
