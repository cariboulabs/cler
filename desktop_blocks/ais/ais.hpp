#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// AIS (ITU-R M.1371) bit level: HDLC deframing with bit unstuffing and the
// CRC-16/X-25 frame check, and the message parser for the position, static
// data and base station reports. No DSP, no allocation.
namespace ais {

struct Message {
    uint8_t type = 0;
    uint32_t mmsi = 0;
    bool has_position = false;
    double lat = 0.0, lon = 0.0;   // degrees
    float sog = 0.0f;              // knots, <0 unknown
    float cog = 0.0f;              // degrees, <0 unknown
    int heading = -1;              // degrees, -1 unknown
    int nav_status = -1;           // type 1/2/3
    char name[21] = {};            // type 5 / 24A
    char callsign[8] = {};         // type 5
    uint8_t ship_type = 0;         // type 5 / 24B
    uint8_t bits_len = 0;          // payload bits / 8 (info)
};

// CRC-16/X-25 (HDLC FCS): poly 0x1021 reflected, init and xorout 0xFFFF.
inline uint16_t crc16_x25(const uint8_t* data, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : static_cast<uint16_t>(crc >> 1);
    }
    return static_cast<uint16_t>(crc ^ 0xFFFF);
}

// Feed NRZI-decoded bits. Collects the bits between HDLC flags (0x7E),
// unstuffs, regroups LSB-first into octets, checks the FCS and hands a valid
// payload to the caller. Frames longer than MAX_BYTES are dropped.
class Deframer {
public:
    static constexpr size_t MAX_BYTES = 128;   // 1008-bit max AIS frame = 126 octets

    // returns true when a CRC-valid payload is ready in payload()/length()
    bool push_bit(bool bit) {
        _flag_shift = static_cast<uint8_t>((_flag_shift << 1) | (bit ? 1u : 0u));
        bool ready = false;
        if (_flag_shift == 0x7E) {
            // the flag's leading 0 and five 1s went into the buffer (the sixth
            // 1 was held back as a possible stuffing position); drop them
            if (_in_frame && _nbits >= 6 + 24) {
                _nbits -= 6;
                ready = finish();
            }
            _in_frame = true;
            _nbits = 0;
            _ones = 0;
            return ready;
        }
        if (!_in_frame) return false;
        if (_ones == 5) {          // after five ones: 0 = stuffed, 1 = sixth one (flag or abort)
            _ones = bit ? 6 : 0;
            return false;
        }
        if (_ones == 6) {          // seven ones = abort (a flag would have matched above)
            _in_frame = false;
            _ones = 0;
            return false;
        }
        _ones = bit ? _ones + 1 : 0;
        if (_nbits >= MAX_BYTES * 8) { _in_frame = false; return false; }
        _bits[_nbits++] = bit;
        return false;
    }

    const uint8_t* payload() const { return _bytes.data(); }
    size_t length() const { return _len; }
    uint32_t frames_ok() const { return _ok; }
    uint32_t frames_bad_crc() const { return _bad; }
    bool in_frame() const { return _in_frame; }

private:
    bool finish() {
        // Drop the partial trailing octet (the bits of the next flag we
        // consumed) and regroup LSB-first.
        const size_t nbytes = _nbits / 8;
        if (nbytes < 4) return false;
        for (size_t i = 0; i < nbytes; ++i) {
            uint8_t b = 0;
            for (int k = 0; k < 8; ++k) b |= static_cast<uint8_t>(_bits[8 * i + k] ? (1u << k) : 0u);
            _bytes[i] = b;
        }
        const uint16_t fcs = static_cast<uint16_t>(_bytes[nbytes - 2] | (_bytes[nbytes - 1] << 8));
        if (crc16_x25(_bytes.data(), nbytes - 2) != fcs) { ++_bad; return false; }
        _len = nbytes - 2;
        ++_ok;
        return true;
    }

    std::array<bool, MAX_BYTES * 8> _bits{};
    std::array<uint8_t, MAX_BYTES> _bytes{};
    size_t _nbits = 0, _len = 0;
    uint8_t _flag_shift = 0;
    int _ones = 0;
    bool _in_frame = false;
    uint32_t _ok = 0, _bad = 0;
};

// Big-endian bit field reader over the payload octets (message bit order).
inline uint32_t bits(const uint8_t* p, size_t len_bits, int start, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        const int b = start + i;
        const uint32_t bit = (static_cast<size_t>(b) < len_bits) ? ((p[b >> 3] >> (7 - (b & 7))) & 1u) : 0u;
        v = (v << 1) | bit;
    }
    return v;
}

inline int32_t sbits(const uint8_t* p, size_t len_bits, int start, int n) {
    uint32_t v = bits(p, len_bits, start, n);
    if (v & (1u << (n - 1))) v |= ~((1u << n) - 1u);
    return static_cast<int32_t>(v);
}

inline void text(const uint8_t* p, size_t len_bits, int start, int nchars, char* out) {
    int w = 0;
    for (int i = 0; i < nchars; ++i) {
        uint32_t c = bits(p, len_bits, start + 6 * i, 6);
        char ch = static_cast<char>(c < 32 ? c + 64 : c);
        if (ch == '@') break;
        out[w++] = ch;
    }
    while (w > 0 && out[w - 1] == ' ') --w;
    out[w] = 0;
}

inline bool parse(const uint8_t* p, size_t nbytes, Message& m) {
    const size_t len_bits = nbytes * 8;
    m = Message{};
    m.bits_len = static_cast<uint8_t>(nbytes);
    m.type = static_cast<uint8_t>(bits(p, len_bits, 0, 6));
    m.mmsi = bits(p, len_bits, 8, 30);
    auto pos = [&](int lon_at, int lat_at) {
        const int32_t lon = sbits(p, len_bits, lon_at, 28), lat = sbits(p, len_bits, lat_at, 27);
        if (lon == 0x6791AC0 || lat == 0x3412140) return;   // 181 / 91 = not available
        m.lon = lon / 600000.0; m.lat = lat / 600000.0;
        m.has_position = (m.lat >= -90.0 && m.lat <= 90.0 && m.lon >= -180.0 && m.lon <= 180.0);
    };
    switch (m.type) {
        case 1: case 2: case 3:
            if (len_bits < 168) return false;
            m.nav_status = static_cast<int>(bits(p, len_bits, 38, 4));
            { const uint32_t sog = bits(p, len_bits, 50, 10); m.sog = sog == 1023 ? -1.0f : sog / 10.0f; }
            pos(61, 89);
            { const uint32_t cog = bits(p, len_bits, 116, 12); m.cog = cog == 3600 ? -1.0f : cog / 10.0f; }
            { const uint32_t hdg = bits(p, len_bits, 128, 9); m.heading = hdg == 511 ? -1 : static_cast<int>(hdg); }
            return true;
        case 4: case 11:
            if (len_bits < 168) return false;
            pos(79, 107);
            return true;
        case 18:
            if (len_bits < 168) return false;
            { const uint32_t sog = bits(p, len_bits, 46, 10); m.sog = sog == 1023 ? -1.0f : sog / 10.0f; }
            pos(57, 85);
            { const uint32_t cog = bits(p, len_bits, 112, 12); m.cog = cog == 3600 ? -1.0f : cog / 10.0f; }
            { const uint32_t hdg = bits(p, len_bits, 124, 9); m.heading = hdg == 511 ? -1 : static_cast<int>(hdg); }
            return true;
        case 5:
            if (len_bits < 420) return false;
            text(p, len_bits, 70, 7, m.callsign);
            text(p, len_bits, 112, 20, m.name);
            m.ship_type = static_cast<uint8_t>(bits(p, len_bits, 232, 8));
            return true;
        case 24:
            if (len_bits < 160) return false;
            if (bits(p, len_bits, 38, 2) == 0) text(p, len_bits, 40, 20, m.name);
            else m.ship_type = static_cast<uint8_t>(bits(p, len_bits, 40, 8));
            return true;
        case 21:
            if (len_bits < 272) return false;
            text(p, len_bits, 43, 20, m.name);
            pos(164, 192);
            return true;
        default:
            return len_bits >= 40;
    }
}

// Encoder for tests and loopback: payload octets (message bit order) ->
// transmitted bit stream: training, flag, stuffed data + FCS, flag; then
// NRZI (0 = transition). Returns the number of bits written.
inline size_t encode_frame(const uint8_t* payload, size_t nbytes, bool* out, size_t max_bits) {
    size_t n = 0;
    auto put = [&](bool b) { if (n < max_bits) out[n++] = b; };
    for (int i = 0; i < 24; ++i) put(i & 1);           // training 0101...
    auto flag = [&]() { for (int i = 0; i < 8; ++i) put((0x7E >> i) & 1); };  // LSB first = 01111110 either way
    flag();
    const uint16_t fcs = crc16_x25(payload, nbytes);
    int ones = 0;
    auto data_bit = [&](bool b) {
        put(b);
        if (b) { if (++ones == 5) { put(false); ones = 0; } } else ones = 0;
    };
    for (size_t i = 0; i < nbytes; ++i) for (int k = 0; k < 8; ++k) data_bit((payload[i] >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> (8 + k)) & 1);
    flag();
    for (int i = 0; i < 24; ++i) put(false);          // buffer
    // NRZI: a 0 flips the line, a 1 keeps it
    bool level = false;
    for (size_t i = 0; i < n; ++i) { if (!out[i]) level = !level; out[i] = level; }
    return n;
}

// NMEA 6-bit armoring (for tests built from real sentences)
inline size_t from_nmea_payload(const char* s, uint8_t* out, size_t max) {
    size_t nbits = 0;
    std::memset(out, 0, max);
    for (const char* c = s; *c; ++c) {
        int v = *c - 48;
        if (v > 40) v -= 8;
        for (int i = 5; i >= 0; --i) {
            if (nbits / 8 >= max) return nbits / 8;
            if ((v >> i) & 1) out[nbits / 8] |= static_cast<uint8_t>(1u << (7 - nbits % 8));
            ++nbits;
        }
    }
    return (nbits + 7) / 8;
}

}  // namespace ais
