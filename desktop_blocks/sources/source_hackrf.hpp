#pragma once
#include "cler.hpp"
#include <hackrf.h>

struct SourceHackRFBlock : public cler::BlockBase {
    SourceHackRFBlock(const char* name,
                      uint64_t freq_hz,
                      uint32_t samp_rate_hz,
                      int lna_gain_db = 16,  // 0-40 dB, multiple of 8
                      int vga_gain_db = 16,  // 0-62 dB, multiple of 2
                      size_t buffer_size = 2 << 22)
        : cler::BlockBase(name),
          _iq(buffer_size),
          _buffer_size(buffer_size)
    {
        if (hackrf_open(&_dev) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to open HackRF device.");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to set frequency.");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to set sample rate.");
        }

        if (hackrf_set_lna_gain(_dev, lna_gain_db) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to set LNA gain.");
        }

        if (hackrf_set_vga_gain(_dev, vga_gain_db) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to set VGA gain.");
        }

        _tmp = new std::complex<float>[buffer_size];
        if (!_tmp) {
            throw std::runtime_error("Failed to allocate memory for temporary buffer.");
        }

        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to start RX streaming.");
        }
    }

    ~SourceHackRFBlock() {
        if (_dev) {
            hackrf_stop_rx(_dev);
            hackrf_close(_dev);
        }
        if (_tmp) {
            delete[] _tmp;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        size_t iq_size = _iq.size();
        size_t out_space = out->space();
        if (iq_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out_space == 0) {
            return cler::Error::NotEnoughSpace;
        }
        size_t transferable = std::min({iq_size, out_space, _buffer_size});

        size_t n = _iq.readN(_tmp, transferable);
        out->writeN(_tmp, n);
        return cler::Empty{};
    }

private:
    hackrf_device* _dev = nullptr;
    cler::Channel<std::complex<float>> _iq;
    std::complex<float>* _tmp = nullptr;
    size_t _buffer_size;

    static int rx_callback(hackrf_transfer* transfer) {
        SourceHackRFBlock* self = static_cast<SourceHackRFBlock*>(transfer->rx_ctx);
        const uint8_t* buf = transfer->buffer;

        for (int i = 0; i < transfer->valid_length; i += 2) {
            float i_sample = (buf[i] - 127.5f) / 127.5f;
            float q_sample = (buf[i + 1] - 127.5f) / 127.5f;
            std::complex<float> sample(i_sample, q_sample);
            bool success = self->_iq.try_push(sample);
            if (!success) {
                continue; // If the queue is full, just skip this sample
            }
        }

        return 0;
    }
};
