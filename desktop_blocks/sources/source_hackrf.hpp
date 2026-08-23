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

#include <atomic>
#include <cstring>

struct SourceHackRFBlock : public cler::BlockBase {
    static constexpr size_t USB_TRANSFER_SAMPLES = 131072;
    static constexpr size_t DEFAULT_RING_SAMPLES = 4 * USB_TRANSFER_SAMPLES;

    SourceHackRFBlock(const char* name,
                      uint64_t freq_hz,
                      uint32_t samp_rate_hz,
                      int lna_gain_db = 40,  // 0-40 dB, multiple of 8
                      int vga_gain_db = 16,  // 0-62 dB, multiple of 2
                      bool amp_enable = false, // Enable RX amp (adds ~14dB)
                      size_t buffer_size = 0,
                      const char* serial = nullptr)  // nullptr = first device
        : cler::BlockBase(name),
          _iq(buffer_size == 0 ? DEFAULT_RING_SAMPLES : buffer_size),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _lna_gain_db(lna_gain_db),
          _vga_gain_db(vga_gain_db),
          _amp_enable(amp_enable)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "Buffer size too small for doubly-mapped buffers. Need at least %zu complex<float> elements",
                cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>));
            cler::panic(msg);
        }

        if (hackrf_init() != HACKRF_SUCCESS) {
            cler::panic("Failed to initialize HackRF library.");
        }

        const int opened = serial && *serial ? hackrf_open_by_serial(serial, &_dev) : hackrf_open(&_dev);
        if (opened != HACKRF_SUCCESS) {
            cler::panic("Failed to open HackRF device.");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set frequency.");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set sample rate.");
        }

        if (hackrf_set_lna_gain(_dev, lna_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set LNA gain.");
        }

        if (hackrf_set_vga_gain(_dev, vga_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set VGA gain.");
        }

        if (hackrf_set_amp_enable(_dev, amp_enable ? 1 : 0) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set amp enable.");
        }

        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to start RX streaming.");
        }

        std::cout << "SourceHackRFBlock: Initialized" << std::endl;
        std::cout << "  Frequency: " << freq_hz / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS" << std::endl;
        std::cout << "  LNA gain: " << lna_gain_db << " dB" << std::endl;
        std::cout << "  VGA gain: " << vga_gain_db << " dB" << std::endl;
        std::cout << "  Amp enabled: " << (amp_enable ? "Yes" : "No") << std::endl;
    }

    ~SourceHackRFBlock() {
        if (_dev) {
            hackrf_stop_rx(_dev);
            hackrf_close(_dev);
        }
        hackrf_exit();

        if (_overflow_count > 0) {
            std::cout << "SourceHackRFBlock: Total overflows: " << _overflow_count << std::endl;
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

    // Opens and closes the device without streaming, so a caller that cannot
    // afford the constructor's panic (busy, unplugged) can ask first.
    static bool can_open(const char* serial = nullptr) {
        if (hackrf_init() != HACKRF_SUCCESS) return false;
        hackrf_device* dev = nullptr;
        const int r = serial && *serial ? hackrf_open_by_serial(serial, &dev) : hackrf_open(&dev);
        if (r == HACKRF_SUCCESS) hackrf_close(dev);
        hackrf_exit();
        return r == HACKRF_SUCCESS;
    }

    uint64_t get_frequency() const { return _freq_hz; }
    uint32_t get_sample_rate() const { return _samp_rate_hz; }
    int get_lna_gain() const { return _lna_gain_db; }
    int get_vga_gain() const { return _vga_gain_db; }
    bool get_amp_enable() const { return _amp_enable; }
    size_t get_overflow_count() const { return _overflow_count.load(); }
    void reset_overflow_count() { _overflow_count.store(0); }
    bool lost() const { return _dev == nullptr || hackrf_is_streaming(_dev) != HACKRF_TRUE; }

    // Setters (be careful - changing these while streaming may cause issues)
    void set_frequency(uint64_t freq_hz) {
        if (_dev && hackrf_set_freq(_dev, freq_hz) == HACKRF_SUCCESS) {
            _freq_hz = freq_hz;
        }
    }

    // Live sample-rate change. hackrf_set_sample_rate reconfigures the sample
    // clock, which is not safe while the RX callback is delivering buffers at
    // the old rate, so we stop RX, apply the new rate, then restart streaming.
    // A few in-flight samples straddling the switch may be garbled once (same
    // caveat as the UHD path). The baseband filter is left at libhackrf's
    // default (as in the constructor), not re-derived here.
    void set_sample_rate(uint32_t samp_rate_hz) {
        if (!_dev || samp_rate_hz == 0 || samp_rate_hz == _samp_rate_hz) {
            return;
        }
        hackrf_stop_rx(_dev);
        if (hackrf_set_sample_rate(_dev, samp_rate_hz) == HACKRF_SUCCESS) {
            _samp_rate_hz = samp_rate_hz;
        } else {
            std::cerr << "SourceHackRFBlock: Failed to set sample rate" << std::endl;
        }
        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            std::cerr << "SourceHackRFBlock: Failed to restart RX after rate change"
                      << std::endl;
        }
    }

    void set_lna_gain(int gain_db) {
        if (_dev && hackrf_set_lna_gain(_dev, gain_db) == HACKRF_SUCCESS) {
            _lna_gain_db = gain_db;
        }
    }

    void set_vga_gain(int gain_db) {
        if (_dev && hackrf_set_vga_gain(_dev, gain_db) == HACKRF_SUCCESS) {
            _vga_gain_db = gain_db;
        }
    }

    void set_amp_enable(bool enable) {
        if (_dev && hackrf_set_amp_enable(_dev, enable ? 1 : 0) == HACKRF_SUCCESS) {
            _amp_enable = enable;
        }
    }

private:
    hackrf_device* _dev = nullptr;
    cler::Channel<std::complex<float>> _iq;
    
    uint64_t _freq_hz;
    uint32_t _samp_rate_hz;
    int _lna_gain_db;
    int _vga_gain_db;
    bool _amp_enable;

    std::atomic<size_t> _overflow_count{0};

    static int rx_callback(hackrf_transfer* transfer) {
        SourceHackRFBlock* self = static_cast<SourceHackRFBlock*>(transfer->rx_ctx);
        const uint8_t* buf = transfer->buffer;

        // HackRF wire format: interleaved signed int8_t IQ, range [-128, 127]
        for (int i = 0; i < transfer->valid_length; i += 2) {
            float i_sample = static_cast<int8_t>(buf[i]) / 128.0f;
            float q_sample = static_cast<int8_t>(buf[i + 1]) / 128.0f;

            std::complex<float> sample(i_sample, q_sample);

            if (!self->_iq.try_push(sample)) {
                self->_overflow_count++;
            }
        }

        return 0;
    }
};