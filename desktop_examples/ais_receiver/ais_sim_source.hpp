#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/ais/ais.hpp"
#include "liquid.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// A handful of ships sailing around Haifa bay, each sending a type 1 position
// report every couple of seconds and a type 5 name every 30 s, GMSK-modulated
// at +/-25 kHz in a 2.4 MS/s stream with noise: the receiver chain sees what
// the HackRF would, minus the hardware.
struct AISSimSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = false;

    struct Ship {
        uint32_t mmsi;
        const char* name;
        const char* callsign;
        double lat, lon, sog_kn, cog_deg;
        double next_pos_s, next_name_s;
    };

    AISSimSourceBlock(const char* name, double sample_rate = 2.4e6, size_t buffer_size = 1 << 18)
        : cler::BlockBase(name), _fs(sample_rate), _out_buffer(buffer_size) {
        // modulate straight at the output rate: a fractional resampler's rate
        // quantisation (~1e-4) drifts a sample over a long frame, unlike real
        // hardware (ppm)
        const double k = sample_rate / 9600.0;
        if (std::fabs(k - std::round(k)) > 1e-9) cler::panic("AISSimSourceBlock: sample_rate must be a multiple of 9600");
        _sps = static_cast<unsigned int>(std::lround(k));
        _mod = gmskmod_create(_sps, 3, 0.4f);
        if (!_mod) cler::panic("AISSimSourceBlock: gmskmod create failed");
        _sym.assign(_sps, {});
        _ships = {{
            {428000101, "HAIFA PILOT", "4XPL1", 32.835, 35.000, 8.0, 250.0, 0.5, 2.0},
            {428000102, "CARMEL STAR", "4XCS2", 32.870, 34.960, 12.5, 180.0, 1.0, 9.0},
            {211000103, "EVER DIADEM", "DDEV", 32.900, 34.900, 14.0, 120.0, 1.5, 16.0},
            {636000104, "MSC AURORA", "D5AU", 32.810, 34.930, 0.0, 0.0, 2.0, 23.0},
            {428000105, "DOLPHIN 3", "4XDL3", 32.845, 35.020, 6.0, 30.0, 2.5, 30.0},
        }};
        _burst.reserve(1 << 18);   // longest frame (1008 bits x 250) fits; no growth in procedure()
    }

    ~AISSimSourceBlock() {
        if (_mod) gmskmod_destroy(_mod);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, wsize] = out->write_dbf();
        size_t n = std::min(wsize, _out_buffer);
        if (n == 0) return cler::Error::NotEnoughSpace;
        for (size_t i = 0; i < n; ++i) {
            std::complex<float> s(_noise(_rng), _noise(_rng));
            if (_burst_pos < _burst.size()) {
                s += _burst[_burst_pos++];
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

    // expose the truth for tests
    const std::vector<Ship>& ships() const { return _ships; }
    size_t in_samples() const { return _samples; }
    const uint8_t* last_payload() const { return _last_payload.data(); }
    size_t last_payload_len() const { return _last_payload_len; }
    // tests: suppress position reports (names only) / names (positions only)
    void set_reports(bool positions, bool names) { for (auto& sh : _ships) { if (!positions) sh.next_pos_s = 1e9; if (!names) sh.next_name_s = 1e9; } }
    void set_noise(float stddev) { _noise = std::normal_distribution<float>(0.0f, stddev); }

private:
    void schedule() {
        // advance ships and pick the next due transmission
        for (auto& sh : _ships) {
            if (_t >= sh.next_pos_s) {
                move(sh, 2.0);
                sh.next_pos_s = _t + 2.0 + 0.3 * _uni(_rng);
                emit(sh, false);
                return;
            }
            if (_t >= sh.next_name_s) {
                sh.next_name_s = _t + 30.0;
                emit(sh, true);
                return;
            }
        }
    }

    static void move(Ship& s, double dt_s) {
        const double d_nm = s.sog_kn * dt_s / 3600.0;
        const double rad = s.cog_deg * M_PI / 180.0;
        s.lat += d_nm / 60.0 * std::cos(rad);
        s.lon += d_nm / 60.0 * std::sin(rad) / std::cos(s.lat * M_PI / 180.0);
    }

    struct BitWriter {
        std::array<uint8_t, 64> bytes{};
        size_t nbits = 0;
        void put(uint32_t v, int n) {
            for (int i = n - 1; i >= 0; --i) { if ((v >> i) & 1u) bytes[nbits / 8] |= static_cast<uint8_t>(1u << (7 - nbits % 8)); ++nbits; }
        }
        void text(const char* s, int nchars) {
            const size_t len = std::strlen(s);
            for (int i = 0; i < nchars; ++i) {
                const char c = static_cast<size_t>(i) < len ? s[i] : '@';
                put(static_cast<uint32_t>(c >= 64 ? c - 64 : c) & 63u, 6);
            }
        }
    };

    void emit(const Ship& s, bool name_report) {
        BitWriter w;
        if (!name_report) {
            w.put(1, 6); w.put(0, 2); w.put(s.mmsi, 30); w.put(s.sog_kn > 0.1 ? 0 : 1, 4); w.put(0, 8);
            w.put(static_cast<uint32_t>(std::lround(s.sog_kn * 10)), 10); w.put(1, 1);
            w.put(static_cast<uint32_t>(std::lround(s.lon * 600000)) & 0x0FFFFFFF, 28);
            w.put(static_cast<uint32_t>(std::lround(s.lat * 600000)) & 0x07FFFFFF, 27);
            w.put(static_cast<uint32_t>(std::lround(s.cog_deg * 10)), 12);
            w.put(static_cast<uint32_t>(std::lround(s.cog_deg)) % 360, 9);
            w.put(static_cast<uint32_t>(std::fmod(_t, 60.0)), 6); w.put(0, 2); w.put(0, 3); w.put(0, 1); w.put(0, 19);
        } else {
            w.put(5, 6); w.put(0, 2); w.put(s.mmsi, 30); w.put(0, 2); w.put(0, 30);
            w.text(s.callsign, 7); w.text(s.name, 20); w.put(70, 8);
            w.put(0, 9); w.put(0, 9); w.put(0, 6); w.put(0, 6); w.put(1, 4); w.put(0, 20); w.put(0, 8); w.text("HAIFA", 20); w.put(0, 1); w.put(0, 1);
        }
        _last_payload = w.bytes; _last_payload_len = (w.nbits + 7) / 8;
        std::array<bool, 2048> tx{};
        const size_t nb = ais::encode_frame(w.bytes.data(), (w.nbits + 7) / 8, tx.data(), tx.size());
        const double offset_hz = (_uni(_rng) < 0.5 ? -25e3 : 25e3) + 400.0 * (_uni(_rng) - 0.5);
        const float amp = 0.15f + 0.25f * _uni(_rng);
        _burst.clear();
        double ph = 0.0;
        const double dph = 2.0 * M_PI * offset_hz / _fs;
        for (size_t i = 0; i < nb; ++i) {
            gmskmod_modulate(_mod, tx[i] ? 1u : 0u, _sym.data());
            for (unsigned int k = 0; k < _sps; ++k) {
                _burst.push_back(amp * _sym[k] * std::complex<float>(static_cast<float>(std::cos(ph)), static_cast<float>(std::sin(ph))));
                ph += dph;
            }
        }
        _burst_pos = 0;
    }

    double _fs, _t = 0.0;
    size_t _samples = 0;
    std::array<uint8_t, 64> _last_payload{};
    size_t _last_payload_len = 0;
    size_t _out_buffer;
    unsigned int _sps = 250;
    gmskmod _mod = nullptr;
    std::vector<liquid_float_complex> _sym;
    std::vector<Ship> _ships;
    std::vector<std::complex<float>> _burst;
    size_t _burst_pos = 0;
    std::mt19937 _rng{7};
    std::normal_distribution<float> _noise{0.0f, 0.02f};
    std::uniform_real_distribution<double> _uni{0.0, 1.0};
};
