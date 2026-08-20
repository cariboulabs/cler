#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/aprs/aprs.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

// A few APRS stations around Haifa beaconing on 144.800 MHz, through the real
// modulator path: AX.25 UI frame -> HDLC + NRZI -> Bell 202 tones -> narrowband
// FM at 2.4 MS/s with noise. The receiver chain sees what the HackRF would,
// minus the hardware. The burst is synthesised sample by sample (2000 samples
// per symbol at 2.4 MS/s would make a 2 M sample buffer otherwise).
struct APRSSimSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = false;
    static constexpr size_t MAX_TX_BITS = 2048;
    static constexpr double MARK_HZ = 1200.0, SPACE_HZ = 2200.0, BAUD = 1200.0;

    struct Station {
        const char* callsign;
        const char* comment;
        double lat, lon;
        double speed_kn, course_deg;
        bool mice;                 // beacon as Mic-E instead of an uncompressed position
        double next_beacon_s;
        double next_status_s;
    };

    APRSSimSourceBlock(const char* name, double sample_rate = 2.4e6, double offset_hz = -250e3,
                       double deviation_hz = 3e3, size_t buffer_size = 1 << 18)
        : cler::BlockBase(name), _fs(sample_rate), _offset(offset_hz), _dev(deviation_hz),
          _out_buffer(buffer_size) {
        const double k = sample_rate / BAUD;
        if (std::fabs(k - std::round(k)) > 1e-9) cler::panic("APRSSimSourceBlock: sample_rate must be a multiple of 1200");
        _sps = static_cast<size_t>(std::lround(k));
        _stations = {{
            {"4X1RF-9", "mobile on Route 4", 32.820, 35.010, 35.0, 120.0, true, 1.0, 1e9},
            {"4Z5DX", "Haifa APRS digi", 32.805, 34.990, 0.0, 0.0, false, 2.0, 8.0},
            {"4X4HF-1", "Carmel weather", 32.780, 35.020, 0.0, 0.0, false, 3.0, 12.0},
            {"4X6TT-7", "handheld walking", 32.850, 34.975, 3.0, 300.0, false, 4.0, 1e9},
        }};
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(wsize, _out_buffer);
        if (n == 0) return cler::Error::NotEnoughSpace;
        for (size_t i = 0; i < n; ++i) {
            std::complex<float> s(_noise(_rng), _noise(_rng));
            if (_pos < _nbits * _sps) {
                const double f = _tx[_pos / _sps] ? MARK_HZ : SPACE_HZ;
                const double audio = std::sin(_tone_ph);
                _tone_ph += 2.0 * M_PI * f / _fs;
                _carrier_ph += 2.0 * M_PI * (_offset + _dev * audio) / _fs;
                s += _amp * std::complex<float>(static_cast<float>(std::cos(_carrier_ph)),
                                                static_cast<float>(std::sin(_carrier_ph)));
                ++_pos;
            } else {
                schedule();
            }
            wptr[i] = s;
            _t += 1.0 / _fs;
        }
        _samples += n;
        out->commit_write(n);
        return cler::Empty{};
    }

    // the truth, for tests
    const std::vector<Station>& stations() const { return _stations; }
    size_t in_samples() const { return _samples; }
    void set_noise(float stddev) { _noise = std::normal_distribution<float>(0.0f, stddev); }

private:
    void schedule() {
        for (auto& st : _stations) {
            if (_t >= st.next_beacon_s) {
                move(st, 5.0);
                st.next_beacon_s = _t + 5.0 + 2.0 * _uni(_rng);
                emit_position(st);
                return;
            }
            if (_t >= st.next_status_s) {
                st.next_status_s = _t + 20.0;
                char info[80];
                std::snprintf(info, sizeof(info), ">%s", st.comment);
                emit(st, "APCLER", info);
                return;
            }
        }
    }

    static void move(Station& s, double dt_s) {
        const double d_nm = s.speed_kn * dt_s / 3600.0;
        const double rad = s.course_deg * M_PI / 180.0;
        s.lat += d_nm / 60.0 * std::cos(rad);
        s.lon += d_nm / 60.0 * std::sin(rad) / std::cos(s.lat * M_PI / 180.0);
    }

    void emit_position(const Station& st) {
        char info[128], dest[8] = "APCLER";
        if (st.mice) {
            aprs::encode_mice(st.lat, st.lon, static_cast<int>(st.speed_kn),
                              static_cast<int>(st.course_deg), '/', '>', dest, info);
        } else {
            const double alat = std::fabs(st.lat), alon = std::fabs(st.lon);
            const int lad = static_cast<int>(alat), lod = static_cast<int>(alon);
            const double lam = (alat - lad) * 60.0, lom = (alon - lod) * 60.0;
            std::snprintf(info, sizeof(info), "!%02d%05.2f%c%c%03d%05.2f%c%c%s",
                          lad, lam, st.lat >= 0 ? 'N' : 'S', '/',
                          lod, lom, st.lon >= 0 ? 'E' : 'W',
                          st.speed_kn > 1.0 ? '>' : '-', st.comment);
        }
        emit(st, dest, info);
    }

    void emit(const Station& st, const char* dest, const char* info) {
        std::array<uint8_t, 160> frame{};
        const size_t nb = aprs::encode_ui(dest, st.callsign, "WIDE1-1", info, frame.data(), frame.size());
        if (nb == 0) return;
        _nbits = aprs::encode_frame(frame.data(), nb, _tx.data(), _tx.size());
        _pos = 0;
        _tone_ph = 0.0;
        _amp = 0.15f + 0.25f * static_cast<float>(_uni(_rng));
    }

    double _fs, _offset, _dev, _t = 0.0;
    size_t _samples = 0, _out_buffer, _sps = 2000;
    std::array<bool, MAX_TX_BITS> _tx{};
    size_t _nbits = 0, _pos = 0;
    double _tone_ph = 0.0, _carrier_ph = 0.0;
    float _amp = 0.3f;
    std::vector<Station> _stations;
    std::mt19937 _rng{11};
    std::normal_distribution<float> _noise{0.0f, 0.02f};
    std::uniform_real_distribution<double> _uni{0.0, 1.0};
};
