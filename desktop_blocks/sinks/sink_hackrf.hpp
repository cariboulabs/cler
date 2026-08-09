#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#ifdef __has_include
    #if __has_include(<libhackrf/hackrf.h>)
        #include <libhackrf/hackrf.h>
    #elif __has_include(<hackrf.h>)
        #include <hackrf.h>
    #else
        #error "HackRF header not found. Please install libhackrf or hackrf-dev package."
    #endif
#endif

#include <complex>
#include <atomic>
#include <cstring>

struct SinkHackRFBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    SinkHackRFBlock(const char* name,
                    uint64_t freq_hz,
                    uint32_t samp_rate_hz,
                    int txvga_gain_db = 0,  // 0-47 dB
                    bool amp_enable = false, // Enable TX amplifier (adds ~10dB but increases harmonics)
                    size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _txvga_gain_db(txvga_gain_db),
          _amp_enable(amp_enable),
          _iq(2 * (buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size))
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        if (hackrf_init() != HACKRF_SUCCESS) {
            cler::panic("Failed to initialize HackRF library");
        }

        if (hackrf_open(&_dev) != HACKRF_SUCCESS) {
            cler::panic("Failed to open HackRF device");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TX frequency");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TX sample rate");
        }

        if (hackrf_set_txvga_gain(_dev, txvga_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TXVGA gain");
        }

        if (hackrf_set_amp_enable(_dev, amp_enable ? 1 : 0) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set amp enable");
        }

        if (hackrf_start_tx(_dev, tx_callback, this) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to start TX streaming");
        }

        std::cout << "SinkHackRFBlock: Initialized" << std::endl;
        std::cout << "  Frequency: " << freq_hz / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS" << std::endl;
        std::cout << "  TXVGA gain: " << txvga_gain_db << " dB" << std::endl;
        std::cout << "  Amp enabled: " << (amp_enable ? "Yes" : "No") << std::endl;
    }

    ~SinkHackRFBlock() {
        if (_dev) {
            hackrf_stop_tx(_dev);
            hackrf_close(_dev);
        }
        hackrf_exit();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = _iq.write_dbf();
        if (write_ptr == nullptr || write_size < 2) {
            return cler::Error::NotEnoughSpace;
        }

        const size_t n = std::min(read_size, write_size / 2);
        for (size_t i = 0; i < n; ++i) {
            const float i_val = std::max(-1.0f, std::min(1.0f, read_ptr[i].real()));
            const float q_val = std::max(-1.0f, std::min(1.0f, read_ptr[i].imag()));
            write_ptr[2 * i]     = static_cast<uint8_t>(static_cast<int8_t>(i_val * 127.0f));
            write_ptr[2 * i + 1] = static_cast<uint8_t>(static_cast<int8_t>(q_val * 127.0f));
        }

        in.commit_read(n);
        _iq.commit_write(2 * n);
        return cler::Empty{};
    }

    size_t get_underrun_count() const { return _underrun_count.load(); }
    void reset_underrun_count() { _underrun_count.store(0); }

private:
    hackrf_device* _dev = nullptr;
    uint64_t _freq_hz;
    uint32_t _samp_rate_hz;
    int _txvga_gain_db;
    bool _amp_enable;

    cler::Channel<uint8_t> _iq;
    std::atomic<size_t> _underrun_count{0};

    static int tx_callback(hackrf_transfer* transfer) {
        SinkHackRFBlock* self = static_cast<SinkHackRFBlock*>(transfer->tx_ctx);
        uint8_t* buf = transfer->buffer;
        const size_t bytes_needed = static_cast<size_t>(transfer->buffer_length);

        auto [read_ptr, read_size] = self->_iq.read_dbf();
        const size_t bytes_to_send = (read_ptr == nullptr) ? 0 : std::min(read_size, bytes_needed);

        if (bytes_to_send > 0) {
            std::memcpy(buf, read_ptr, bytes_to_send);
            self->_iq.commit_read(bytes_to_send);
        }

        if (bytes_to_send < bytes_needed) {
            std::memset(&buf[bytes_to_send], 0, bytes_needed - bytes_to_send);
            self->_underrun_count++;
        }

        return 0;
    }
};