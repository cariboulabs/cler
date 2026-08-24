#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#ifdef __has_include
    #if __has_include(<iio.h>)
        #include <iio.h>
    #else
        #error "libiio header not found. Please install libiio-dev package."
    #endif
#endif

#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// PlutoSDR (and any AD936x/IIO device) RX source via libiio.
// Works with the same code over any libiio URI:
//   "ip:192.168.2.1"  - Pluto over USB-ethernet / network
//   "usb:"            - Pluto over raw USB
//   "local:"          - running ON the Pluto itself
enum class PlutoAgcMode { FastAttack, SlowAttack };

inline const char* to_iio_gain_control_mode(PlutoAgcMode mode) {
    switch (mode) {
        case PlutoAgcMode::FastAttack: return "fast_attack";
        case PlutoAgcMode::SlowAttack: return "slow_attack";
    }
    cler::panic("Pluto: unknown PlutoAgcMode");
}

struct SourcePlutoBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    struct Probe {
        bool ok = false;
        bool reached = false;   // the context opened; ok still false means no rx device on it
        long long fmin = 70000000, fmax = 6000000000;
        long long rmin = 2083333, rmax = 61440000;
    };

    // Non-panicking availability check; also reads what the driver will accept
    // so callers can clamp before the panicking constructor runs.
    static Probe probe(const char* uri) {
        Probe pr;
        iio_context* ctx = iio_create_context_from_uri(uri);
        if (!ctx) return pr;
        pr.reached = true;
        iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
        iio_device* rx_dev = iio_context_find_device(ctx, "cf-ad9361-lpc");
        if (phy && rx_dev) {
            pr.ok = true;
            char buf[128];
            iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);
            if (lo && iio_channel_attr_read(lo, "frequency_available", buf, sizeof buf) > 0) {
                read_range(buf, pr.fmin, pr.fmax);
            }
            iio_channel* chn = iio_device_find_channel(phy, "voltage0", false);
            if (chn && iio_channel_attr_read(chn, "sampling_frequency_available", buf, sizeof buf) > 0) {
                read_range(buf, pr.rmin, pr.rmax);
                // the driver rejects exactly the advertised minimum (it rounds the
                // internal divider below the floor) but accepts min+1 and then
                // reports the minimum back, so a caller clamping to rmin fails
                ++pr.rmin;
            }
        }
        iio_context_destroy(ctx);
        return pr;
    }

    SourcePlutoBlock(const char* name,
                     const char* uri,          // e.g. "ip:192.168.2.1"
                     long long freq_hz,
                     long long samp_rate_hz,
                     double gain_db = -1.0,    // <0 => AGC (agc_mode), >=0 => manual gain
                     long long bandwidth_hz = 0, // 0 => same as sample rate
                     size_t buffer_size = 1 << 14,
                     PlutoAgcMode agc_mode = PlutoAgcMode::FastAttack)
        : cler::BlockBase(name),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _buffer_size(buffer_size)
    {
        if (bandwidth_hz == 0) bandwidth_hz = samp_rate_hz;

        _ctx = iio_create_context_from_uri(uri);
        if (!_ctx) {
            std::string msg = std::string("Pluto: failed to create IIO context for uri: ") + uri;
            cler::panic(msg.c_str());
        }

        iio_device* phy = iio_context_find_device(_ctx, "ad9361-phy");
        iio_device* rx_dev = iio_context_find_device(_ctx, "cf-ad9361-lpc");
        if (!phy || !rx_dev) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: ad9361-phy / cf-ad9361-lpc not found (not a Pluto?)");
        }

        // RX LO frequency lives on output channel altvoltage0 of the phy
        iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);
        // RX chain config lives on input channel voltage0 of the phy
        iio_channel* chn = iio_device_find_channel(phy, "voltage0", false);
        if (!lo || !chn) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: phy channels not found");
        }

        write_attr_or_panic(lo, "frequency", freq_hz);
        write_attr_or_panic(chn, "sampling_frequency", samp_rate_hz);
        write_attr_or_panic(chn, "rf_bandwidth", bandwidth_hz);

        long long actual_rate = 0;
        if (iio_channel_attr_read_longlong(chn, "sampling_frequency", &actual_rate) < 0 ||
            std::llabs(actual_rate - samp_rate_hz) > samp_rate_hz / 100) {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "Pluto: driver clamped sample rate to %lld Hz (requested %lld)",
                          actual_rate, samp_rate_hz);
            iio_context_destroy(_ctx);
            cler::panic(msg);
        }
        _lo = lo;
        _chn = chn;

        if (gain_db < 0.0) {
            iio_channel_attr_write(chn, "gain_control_mode", to_iio_gain_control_mode(agc_mode));
        } else {
            iio_channel_attr_write(chn, "gain_control_mode", "manual");
            iio_channel_attr_write_double(chn, "hardwaregain", gain_db);
        }

        _rx_i = iio_device_find_channel(rx_dev, "voltage0", false);
        _rx_q = iio_device_find_channel(rx_dev, "voltage1", false);
        if (!_rx_i || !_rx_q) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: RX streaming channels not found");
        }
        iio_channel_enable(_rx_i);
        iio_channel_enable(_rx_q);

        _buf = iio_device_create_buffer(rx_dev, buffer_size, false);
        if (!_buf) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: failed to create RX buffer");
        }

        std::cout << "SourcePlutoBlock: Initialized (" << uri << ")\n"
                  << "  Frequency: " << freq_hz / 1e6 << " MHz\n"
                  << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS\n"
                  << "  Bandwidth: " << bandwidth_hz / 1e6 << " MHz\n"
                  << "  Gain: " << (gain_db < 0.0 ? std::string("AGC (") + to_iio_gain_control_mode(agc_mode) + ")"
                                                  : std::to_string(gain_db) + " dB")
                  << std::endl;
    }

    ~SourcePlutoBlock() {
        if (_buf) iio_buffer_destroy(_buf);
        if (_ctx) iio_context_destroy(_ctx);
    }

    SourcePlutoBlock(const SourcePlutoBlock&) = delete;
    SourcePlutoBlock& operator=(const SourcePlutoBlock&) = delete;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        // Refill from hardware when the previous buffer is fully consumed.
        // iio_buffer_refill blocks for ~one buffer duration - fine, this block
        // runs on its own flowgraph thread.
        if (_consumed >= _available) {
            ssize_t nbytes = iio_buffer_refill(_buf);
            if (nbytes < 0) {
                _lost = true;
                return cler::Error::ProcedureError;
            }
            _available = static_cast<size_t>(nbytes) / (2 * sizeof(int16_t));
            _consumed = 0;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        const int16_t* samples = static_cast<const int16_t*>(iio_buffer_start(_buf));
        size_t n = std::min(_available - _consumed, write_size);
        convert_i16_to_cf32(samples + 2 * _consumed, write_ptr, n);
        _consumed += n;
        out->commit_write(n);
        return cler::Empty{};
    }

    long long get_frequency() const { return _freq_hz; }
    long long get_sample_rate() const { return _samp_rate_hz; }
    bool lost() const { return _lost; }

    double get_gain() const {
        double g = 0.0;
        if (_chn) iio_channel_attr_read_double(_chn, "hardwaregain", &g);
        return g;
    }

    bool get_agc() const {
        char buf[32];
        if (_chn && iio_channel_attr_read(_chn, "gain_control_mode", buf, sizeof buf) > 0) {
            return std::string(buf) != "manual";
        }
        return false;
    }

    void set_gain(double gain_db) {
        if (!_chn) return;
        iio_channel_attr_write(_chn, "gain_control_mode", "manual");
        iio_channel_attr_write_double(_chn, "hardwaregain", gain_db);
    }

    void set_agc(bool on, PlutoAgcMode mode = PlutoAgcMode::FastAttack) {
        if (!_chn) return;
        iio_channel_attr_write(_chn, "gain_control_mode", on ? to_iio_gain_control_mode(mode) : "manual");
    }

    // Retune while streaming (phy attrs are safe to write at runtime)
    void set_frequency(long long freq_hz) {
        if (_lo && iio_channel_attr_write_longlong(_lo, "frequency", freq_hz) >= 0) {
            _freq_hz = freq_hz;
        }
    }

private:
    static void read_range(const char* buf, long long& lo_out, long long& hi_out) {
        long long a = 0, b = 0, c = 0;
        if (std::sscanf(buf, "[%lld %lld %lld]", &a, &b, &c) == 3) { lo_out = a; hi_out = c; }
    }

    static void convert_i16_to_cf32(const int16_t* src, std::complex<float>* dst, size_t n) {
        constexpr float scale = 1.0f / 2048.0f;
        size_t i = 0;
#if defined(__ARM_NEON)
        float* out = reinterpret_cast<float*>(dst);
        const size_t n_floats = 2 * n;
        for (; i + 8 <= n_floats; i += 8) {
            int16x8_t v = vld1q_s16(src + i);
            float32x4_t lo = vcvtq_f32_s32(vmovl_s16(vget_low_s16(v)));
            float32x4_t hi = vcvtq_f32_s32(vmovl_s16(vget_high_s16(v)));
            vst1q_f32(out + i,     vmulq_n_f32(lo, scale));
            vst1q_f32(out + i + 4, vmulq_n_f32(hi, scale));
        }
        i /= 2;
#endif
        for (; i < n; ++i) {
            dst[i] = std::complex<float>(src[2 * i] * scale, src[2 * i + 1] * scale);
        }
    }

    void write_attr_or_panic(iio_channel* ch, const char* attr, long long val) {
        int ret = iio_channel_attr_write_longlong(ch, attr, val);
        if (ret < 0) {
            char err[64];
            iio_strerror(-ret, err, sizeof(err));
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Pluto: failed to set %s to %lld: %s", attr, val, err);
            iio_context_destroy(_ctx);
            cler::panic(msg);
        }
    }

    iio_context* _ctx = nullptr;
    iio_buffer* _buf = nullptr;
    iio_channel* _rx_i = nullptr;
    iio_channel* _rx_q = nullptr;
    iio_channel* _lo = nullptr;
    iio_channel* _chn = nullptr;
    bool _lost = false;

    long long _freq_hz;
    long long _samp_rate_hz;
    size_t _buffer_size;

    size_t _available = 0; // samples in the current iio buffer
    size_t _consumed = 0;  // samples already pushed downstream
};
