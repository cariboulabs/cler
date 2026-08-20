#pragma once

#include "desktop_blocks/ais/ais.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// APRS over AX.25 UI frames, bit level: the HDLC layer (flags, bit stuffing,
// CRC-16/X-25 FCS, LSB-first octets) is identical to AIS, so ais::Deframer and
// ais::crc16_x25 are reused as-is. On top of them: AX.25 address decoding, the
// UI control/PID check, and the APRS info-field parser. No DSP, no allocation.
//
// ais::Deframer caps a frame at 126 payload octets, which is well past a
// typical APRS packet (addresses + <= ~80 info bytes) but short of AX.25's
// 256-byte maximum info field; longer frames are dropped.
namespace aprs {

using ais::crc16_x25;
using ais::Deframer;

constexpr size_t MAX_PATH = 8;      // AX.25 allows 8 digipeaters
constexpr uint8_t UI_CONTROL = 0x03, UI_PID = 0xF0;

struct Packet {
    char source[10] = {};       // CALL-SS
    char dest[10] = {};
    char path[80] = {};         // digipeaters, comma separated, '*' = used
    char type = 0;              // APRS data type identifier
    bool has_position = false;
    double lat = 0.0, lon = 0.0;    // degrees, +N +E
    float course = -1.0f;       // degrees, <0 unknown
    float speed = -1.0f;        // knots, <0 unknown
    bool has_altitude = false;
    int altitude_ft = 0;
    char symbol_table = 0, symbol_code = 0;
    char comment[64] = {};
    char info[128] = {};        // raw info field
    uint8_t info_len = 0;
};

// ---- AX.25 addresses -------------------------------------------------------

// 6 callsign characters shifted left by one, then an SSID octet whose bit 0 is
// the end-of-address extension. Returns false on a malformed callsign.
inline bool decode_address(const uint8_t* a, char* out, bool* last, bool* repeated = nullptr) {
    int w = 0;
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<char>(a[i] >> 1);
        if (c == ' ') continue;
        if (c < '0' || c > 'Z' || (c > '9' && c < 'A')) return false;
        out[w++] = c;
    }
    if (w == 0) return false;
    const int ssid = (a[6] >> 1) & 0x0F;
    if (ssid) {
        out[w++] = '-';
        if (ssid >= 10) out[w++] = '1';
        out[w++] = static_cast<char>('0' + ssid % 10);
    }
    out[w] = 0;
    *last = (a[6] & 1) != 0;
    if (repeated) *repeated = (a[6] & 0x80) != 0;
    return true;
}

inline void encode_address(const char* call, int ssid, bool last, uint8_t* out) {
    const size_t n = std::strlen(call);
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<size_t>(i) < n ? call[i] : ' ';
        out[i] = static_cast<uint8_t>(c << 1);
    }
    out[6] = static_cast<uint8_t>(0x60 | ((ssid & 0x0F) << 1) | (last ? 1 : 0));
}

// ---- APRS info field -------------------------------------------------------

namespace detail {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline int num(const char* s, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (!is_digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// "DDMM.hhN/DDDMM.hhW$" -> lat, lon, symbol. 19 characters.
inline bool uncompressed_position(const char* s, size_t n, Packet& p) {
    if (n < 19) return false;
    const int lat_d = num(s, 2), lat_m = num(s + 2, 2);
    if (lat_d < 0 || lat_m < 0 || s[4] != '.' || !is_digit(s[5])) return false;
    // minutes may be ambiguity-blanked ("33  .  N"); the digits we have suffice
    const int lat_h = is_digit(s[6]) ? (s[5] - '0') * 10 + (s[6] - '0') : (s[5] - '0') * 10;
    const int lon_d = num(s + 9, 3), lon_m = num(s + 12, 2);
    if (lon_d < 0 || lon_m < 0 || s[14] != '.' || !is_digit(s[15])) return false;
    const int lon_h = is_digit(s[16]) ? (s[15] - '0') * 10 + (s[16] - '0') : (s[15] - '0') * 10;
    if ((s[7] != 'N' && s[7] != 'S') || (s[17] != 'E' && s[17] != 'W')) return false;
    p.lat = lat_d + (lat_m + lat_h / 100.0) / 60.0;
    p.lon = lon_d + (lon_m + lon_h / 100.0) / 60.0;
    if (s[7] == 'S') p.lat = -p.lat;
    if (s[17] == 'W') p.lon = -p.lon;
    p.symbol_table = s[8];
    p.symbol_code = s[18];
    p.has_position = true;
    return true;
}

// base-91 compressed position: "/YYYYXXXX$csT", 13 characters
inline bool compressed_position(const char* s, size_t n, Packet& p) {
    // the leading octet is the symbol table id; anything else is a malformed
    // report, not a compressed one
    if (n < 13) return false;
    if (!(s[0] == '/' || s[0] == '\\' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'j'))) return false;
    auto b91 = [&](int at, int len) {
        long v = 0;
        for (int i = 0; i < len; ++i) {
            if (s[at + i] < '!' || s[at + i] > '{') return -1L;
            v = v * 91 + (s[at + i] - 33);
        }
        return v;
    };
    const long y = b91(1, 4), x = b91(5, 4);
    if (y < 0 || x < 0) return false;
    p.lat = 90.0 - y / 380926.0;
    p.lon = -180.0 + x / 190463.0;
    p.symbol_table = s[0];
    p.symbol_code = s[9];
    p.has_position = true;
    // cs: course/speed when c is in '!'..'z' and the compression type says so
    if (s[10] >= '!' && s[10] <= 'z' && s[11] >= '!' && s[11] <= '{') {
        p.course = static_cast<float>((s[10] - 33) * 4);
        p.speed = static_cast<float>(std::pow(1.08, s[11] - 33) - 1.0);
    }
    return true;
}

inline bool position(const char* s, size_t n, Packet& p) {
    if (n == 0) return false;
    return is_digit(s[0]) ? uncompressed_position(s, n, p) : compressed_position(s, n, p);
}

// "/A=001234" anywhere in the comment (feet), and a leading "ddd/sss"
// course/speed extension.
inline void comment_extras(const char* s, size_t n, Packet& p) {
    if (n >= 7 && s[3] == '/' && num(s, 3) >= 0 && num(s + 4, 3) >= 0) {
        const int crs = num(s, 3), spd = num(s + 4, 3);
        if (crs > 0 && crs <= 360) p.course = static_cast<float>(crs % 360);
        p.speed = static_cast<float>(spd);
        s += 7;
        n -= 7;
    }
    for (size_t i = 0; i + 9 <= n; ++i) {
        if (s[i] == '/' && s[i + 1] == 'A' && s[i + 2] == '=') {
            const int alt = num(s + i + 3, 6);
            if (alt >= 0) { p.altitude_ft = alt; p.has_altitude = true; }
            break;
        }
    }
    size_t w = 0;
    for (size_t i = 0; i < n && w + 1 < sizeof(p.comment); ++i) {
        if (static_cast<unsigned char>(s[i]) >= 0x20) p.comment[w++] = s[i];
    }
    while (w > 0 && p.comment[w - 1] == ' ') --w;
    p.comment[w] = 0;
}

// Mic-E destination character: latitude digit plus the message/hemisphere bit.
// 'K', 'L' and 'Z' are position-ambiguity blanks.
inline int mice_digit(char c, int* bit) {
    if (c >= '0' && c <= '9') { *bit = 0; return c - '0'; }
    if (c >= 'A' && c <= 'J') { *bit = 1; return c - 'A'; }   // custom message
    if (c >= 'P' && c <= 'Y') { *bit = 1; return c - 'P'; }   // standard message
    if (c == 'K' || c == 'L' || c == 'Z') { *bit = c == 'L' ? 0 : 1; return 0; }
    return -1;
}

// dest = the 6 raw callsign characters; info = the field after the '`'/'\''
// data type identifier (>= 8 bytes: lon d/m/h, SP/DC/SE, symbol, table).
inline bool mice(const char* dest, const char* info, size_t n, Packet& p) {
    if (n < 8) return false;
    int d[6], bit[6];
    for (int i = 0; i < 6; ++i) {
        d[i] = mice_digit(dest[i], &bit[i]);
        if (d[i] < 0) return false;
    }
    p.lat = d[0] * 10 + d[1] + (d[2] * 10 + d[3] + (d[4] * 10 + d[5]) / 100.0) / 60.0;
    if (!bit[3]) p.lat = -p.lat;                       // byte 4: 0-9 = south

    int lon_d = static_cast<unsigned char>(info[0]) - 28;
    if (bit[4]) lon_d += 100;                          // byte 5: P-Z = +100 deg
    if (lon_d >= 180 && lon_d <= 189) lon_d -= 80;
    else if (lon_d >= 190 && lon_d <= 199) lon_d -= 190;
    if (lon_d < 0 || lon_d > 179) return false;
    int lon_m = static_cast<unsigned char>(info[1]) - 28;
    if (lon_m >= 60) lon_m -= 60;
    const int lon_h = static_cast<unsigned char>(info[2]) - 28;
    if (lon_m < 0 || lon_m > 59 || lon_h < 0 || lon_h > 99) return false;
    p.lon = lon_d + (lon_m + lon_h / 100.0) / 60.0;
    if (bit[5]) p.lon = -p.lon;                        // byte 6: P-Z = west

    const int sp = static_cast<unsigned char>(info[3]) - 28;
    const int dc = static_cast<unsigned char>(info[4]) - 28;
    const int se = static_cast<unsigned char>(info[5]) - 28;
    if (sp < 0 || dc < 0 || se < 0) return false;
    int speed = sp * 10 + dc / 10;
    int course = (dc % 10) * 100 + se;
    if (speed >= 800) speed -= 800;
    if (course >= 400) course -= 400;
    p.speed = static_cast<float>(speed);
    p.course = static_cast<float>(course);
    p.symbol_code = info[6];
    p.symbol_table = info[7];
    p.has_position = true;
    if (n > 8) comment_extras(info + 8, n - 8, p);
    return true;
}

}  // namespace detail

// ---- frame parser ----------------------------------------------------------

// AX.25 UI frame octets (FCS already stripped by the deframer) -> Packet.
inline bool parse(const uint8_t* b, size_t n, Packet& p) {
    p = Packet{};
    if (n < 16) return false;
    char raw_dest[7] = {};
    for (int i = 0; i < 6; ++i) raw_dest[i] = static_cast<char>(b[i] >> 1);
    bool last = false;
    if (!decode_address(b, p.dest, &last) || last) return false;
    if (!decode_address(b + 7, p.source, &last)) return false;
    size_t at = 14;
    size_t w = 0;
    for (size_t i = 0; i < MAX_PATH && !last; ++i) {
        if (at + 7 > n) return false;
        char call[10];
        bool repeated = false;
        if (!decode_address(b + at, call, &last, &repeated)) return false;
        at += 7;
        const size_t len = std::strlen(call);
        if (w + len + 2 < sizeof(p.path)) {
            if (w) p.path[w++] = ',';
            std::memcpy(p.path + w, call, len);
            w += len;
            if (repeated) p.path[w++] = '*';
        }
    }
    p.path[w] = 0;
    if (!last || at + 2 > n) return false;
    if (b[at] != UI_CONTROL || b[at + 1] != UI_PID) return false;
    at += 2;

    const char* info = reinterpret_cast<const char*>(b + at);
    const size_t ilen = n - at;
    p.info_len = static_cast<uint8_t>(std::min(ilen, sizeof(p.info) - 1));
    std::memcpy(p.info, info, p.info_len);
    if (ilen == 0) return true;
    p.type = info[0];
    switch (p.type) {
        case '!': case '=':
            if (detail::position(info + 1, ilen - 1, p)) {
                const size_t used = detail::is_digit(info[1]) ? 20u : 14u;
                if (ilen > used) detail::comment_extras(info + used, ilen - used, p);
            }
            return true;
        case '@': case '/':
            if (ilen < 8) return true;
            if (detail::position(info + 8, ilen - 8, p)) {
                const size_t used = detail::is_digit(info[8]) ? 27u : 21u;
                if (ilen > used) detail::comment_extras(info + used, ilen - used, p);
            }
            return true;
        case '`': case '\'': case 0x1C: case 0x1D:
            detail::mice(raw_dest, info + 1, ilen - 1, p);
            return true;
        case '>':
            detail::comment_extras(info + 1, ilen - 1, p);
            return true;
        default:
            return true;
    }
}

// ---- encoder (tests, loopback and the simulator) ---------------------------

// Build the AX.25 UI frame octets: dest, source, digipeaters, 0x03, 0xF0, info.
// `path` is comma separated ("WIDE1-1,WIDE2-1"), may be empty.
inline size_t encode_ui(const char* dest, const char* source, const char* path,
                        const char* info, uint8_t* out, size_t max) {
    auto split_ssid = [](const char* s, char* call, int* ssid) {
        const char* dash = std::strchr(s, '-');
        const size_t n = dash ? static_cast<size_t>(dash - s) : std::strlen(s);
        std::memcpy(call, s, n);
        call[n] = 0;
        *ssid = dash ? std::atoi(dash + 1) : 0;
    };
    char call[16];
    int ssid = 0;
    size_t at = 0;
    if (max < 16) return 0;
    split_ssid(dest, call, &ssid);
    encode_address(call, ssid, false, out + at);
    at += 7;
    split_ssid(source, call, &ssid);
    encode_address(call, ssid, path && path[0] ? false : true, out + at);
    at += 7;
    for (const char* s = path; s && *s;) {
        const char* comma = std::strchr(s, ',');
        char one[16] = {};
        const size_t n = comma ? static_cast<size_t>(comma - s) : std::strlen(s);
        if (n >= sizeof(one) || at + 7 > max) return 0;
        std::memcpy(one, s, n);
        split_ssid(one, call, &ssid);
        s = comma ? comma + 1 : s + n;
        encode_address(call, ssid, *s == 0, out + at);
        at += 7;
    }
    const size_t ilen = std::strlen(info);
    if (at + 2 + ilen > max) return 0;
    out[at++] = UI_CONTROL;
    out[at++] = UI_PID;
    std::memcpy(out + at, info, ilen);
    return at + ilen;
}

// Build a Mic-E beacon: the latitude, hemispheres and longitude offset live in
// the destination callsign, everything else in the info field. Message code 0
// (M0, "off duty"); speed in knots, course in degrees.
inline void encode_mice(double lat, double lon, int speed_kn, int course_deg,
                        char sym_table, char sym_code, char* dest, char* info) {
    const double alat = std::fabs(lat);
    const int d[6] = {
        static_cast<int>(alat) / 10, static_cast<int>(alat) % 10,
        static_cast<int>(alat * 60.0) % 60 / 10, static_cast<int>(alat * 60.0) % 60 % 10,
        static_cast<int>(std::lround(alat * 6000.0)) % 100 / 10,
        static_cast<int>(std::lround(alat * 6000.0)) % 100 % 10,
    };
    const int alon_d = static_cast<int>(std::fabs(lon));
    // pick the longitude-degrees octet and the +100 offset flag that decode
    // back to alon_d (the spec's ranges are easier to search than to invert)
    int lon_byte = 28, offset = 0;
    for (offset = 0; offset < 2; ++offset) {
        for (lon_byte = 28; lon_byte < 128; ++lon_byte) {
            int v = lon_byte - 28 + (offset ? 100 : 0);
            if (v >= 180 && v <= 189) v -= 80;
            else if (v >= 190 && v <= 199) v -= 190;
            if (v == alon_d) goto found;
        }
    }
found:
    const int bit[6] = {1, 1, 1, lat >= 0.0 ? 1 : 0, offset, lon < 0.0 ? 1 : 0};
    for (int i = 0; i < 6; ++i) dest[i] = static_cast<char>((bit[i] ? 'P' : '0') + d[i]);
    dest[6] = 0;

    const double amin = (std::fabs(lon) - alon_d) * 60.0;
    const int lm = static_cast<int>(amin);
    const int lh = static_cast<int>(std::lround((amin - lm) * 100.0)) % 100;
    const int sp = speed_kn / 10, dc = (speed_kn % 10) * 10 + course_deg / 100, se = course_deg % 100;
    int w = 0;
    info[w++] = '`';
    info[w++] = static_cast<char>(lon_byte);
    info[w++] = static_cast<char>(lm + (lm < 10 ? 88 : 28));
    info[w++] = static_cast<char>(lh + 28);
    info[w++] = static_cast<char>(sp + 28 < 33 ? sp + 108 : sp + 28);
    info[w++] = static_cast<char>(dc + 28);
    info[w++] = static_cast<char>(se + 28);
    info[w++] = sym_code;
    info[w++] = sym_table;
    info[w] = 0;
}

// Transmitted bit stream: `nflags` HDLC flags of preamble (what a real TNC
// keys up with, unlike AIS's 0101 training), stuffed data + FCS, closing
// flags; then NRZI (0 = transition). Returns the number of bits written.
inline size_t encode_frame(const uint8_t* payload, size_t nbytes, bool* out, size_t max_bits, int nflags = 16) {
    size_t n = 0;
    auto put = [&](bool b) { if (n < max_bits) out[n++] = b; };
    auto flag = [&]() { for (int i = 0; i < 8; ++i) put((0x7E >> i) & 1); };
    for (int i = 0; i < nflags; ++i) flag();
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
    flag();
    bool level = false;
    for (size_t i = 0; i < n; ++i) { if (!out[i]) level = !level; out[i] = level; }
    return n;
}

}  // namespace aprs
