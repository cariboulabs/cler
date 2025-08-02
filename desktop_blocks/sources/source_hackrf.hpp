#pragma once
#include "cler.hpp"

#ifdef __has_include
    #if __has_include(<libhackrf/hackrf.h>)
        #include <libhackrf/hackrf.h>
    #elif __has_include(<hackrf.h>)
        #include <hackrf.h>
    #else
        #error "HackRF header not found. Please install libhackrf or hackrf-dev package."
    #endif
#endif

struct SourceHackRFBlock : public cler::BlockBase {
    SourceHackRFBlock(const char* name,
                      uint64_t freq_hz,
                      uint32_t samp_rate_hz,
                      int lna_gain_db = 16,  // 0-40 dB, multiple of 8
                      int vga_gain_db = 16,  // 0-62 dB, multiple of 2
                      size_t buffer_size = 0)
        : cler::BlockBase(name),
          _iq(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _buffer_size(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size)
    {
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) + " complex<float> elements");
        }
        if (hackrf_open(&_dev) != HACKRF_SUCCESS) {
            throw std::runtime_error("Failed to open HackRF device.");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            throw std::runtime_error("Failed to set frequency.");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            throw std::runtime_error("Failed to set sample rate.");
        }

        if (hackrf_set_lna_gain(_dev, lna_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            throw std::runtime_error("Failed to set LNA gain.");
        }

        if (hackrf_set_vga_gain(_dev, vga_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            throw std::runtime_error("Failed to set VGA gain.");
        }

        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            throw std::runtime_error("Failed to start RX streaming.");
        }
    }

    ~SourceHackRFBlock() {
        if (_dev) {
            hackrf_stop_rx(_dev);
            hackrf_close(_dev);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [read_ptr, read_size] = _iq.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        size_t to_copy = std::min(read_size, write_size);
        std::memcpy(write_ptr, read_ptr, to_copy * sizeof(std::complex<float>));
        _iq.commit_read(to_copy);
        out->commit_write(to_copy);
        return cler::Empty{};
    }

private:
    hackrf_device* _dev = nullptr;
    cler::Channel<std::complex<float>> _iq;
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
