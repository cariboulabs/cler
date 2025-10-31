#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>

struct FMDemodBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    // FM demodulator using liquid-dsp's freqdem
    // Parameters:
    //   sample_rate    : SDR sample rate in Hz (e.g., 2e6 for 2 MSPS)
    //   freq_deviation : FM frequency deviation in Hz (default: 75 kHz for broadcast)
    FMDemodBlock(const char* name,
                 double sample_rate,
                 double freq_deviation = 75e3,
                 size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _sample_rate(sample_rate),
          _freq_deviation(freq_deviation)
    {
        // Validate buffer size for DBF
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument(
                "Buffer size too small for doubly-mapped buffers. Need at least " +
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) +
                " complex<float> elements");
        }

        if (sample_rate <= 0.0) {
            throw std::invalid_argument("Sample rate must be positive");
        }

        if (freq_deviation <= 0.0) {
            throw std::invalid_argument("Frequency deviation must be positive");
        }

        // Create FM demodulator: kf = freq_deviation / sample_rate
        float kf = static_cast<float>(freq_deviation / sample_rate);
        _demod = freqdem_create(kf);

        if (!_demod) {
            throw std::runtime_error("Failed to create FM demodulator");
        }
    }

    ~FMDemodBlock() {
        if (_demod) {
            freqdem_destroy(_demod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Read from input using DBF (zero-copy)
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Write to output using DBF (zero-copy)
        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Process limited by available input, output space, and buffer size
        size_t samples_to_process = std::min({read_size, write_space});

        // Demodulate directly into output buffer
        freqdem_demodulate_block(
            _demod,
            reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)),
            samples_to_process,
            reinterpret_cast<float*>(write_ptr));

        in.commit_read(samples_to_process);
        out->commit_write(samples_to_process);

        return cler::Empty{};
    }

private:

    freqdem _demod = nullptr;
    double _sample_rate;
    double _freq_deviation;
};
