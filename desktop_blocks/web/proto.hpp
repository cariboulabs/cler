#pragma once

#include "desktop_blocks/spectrum/spectrum_frame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "websdr protocol v1 is little-endian on the wire");

namespace web {

constexpr uint8_t PROTO_VER = 1;
constexpr uint8_t T_SPECTRUM = 0x01;
constexpr uint8_t T_AUDIO = 0x02;
constexpr uint8_t CODEC_PCM16_48K = 0;
constexpr size_t HEADER_BYTES = 10;
constexpr size_t SPECTRUM_HEAD_BYTES = HEADER_BYTES + 8 + 8 + 2 + 4 + 4;
constexpr size_t AUDIO_HEAD_BYTES = HEADER_BYTES + 1;
constexpr size_t AUDIO_CHUNK = 960;

struct Header {
    uint8_t type = 0, ver = 0;
    uint32_t gen = 0, seq = 0;
};

namespace detail {
template <typename T> inline void put(uint8_t*& p, const T& v) { std::memcpy(p, &v, sizeof(T)); p += sizeof(T); }
template <typename T> inline void get(const uint8_t*& p, T& v) { std::memcpy(&v, p, sizeof(T)); p += sizeof(T); }
inline void put_header(uint8_t*& p, uint8_t type, uint32_t gen, uint32_t seq) {
    put(p, type); put(p, PROTO_VER); put(p, gen); put(p, seq);
}
}

inline size_t encode_spectrum(const SpectrumFrame& f, uint32_t seq, uint8_t* out, size_t cap) {
    const size_t need = SPECTRUM_HEAD_BYTES + f.n;
    if (cap < need || f.n > sizeof(f.bins)) return 0;
    uint8_t* p = out;
    detail::put_header(p, T_SPECTRUM, f.gen, seq);
    detail::put(p, f.center_hz);
    detail::put(p, f.rate_hz);
    detail::put(p, f.n);
    detail::put(p, f.db_min);
    detail::put(p, f.db_step);
    std::memcpy(p, f.bins, f.n);
    return need;
}

inline size_t encode_audio(uint32_t gen, uint32_t seq, const int16_t* pcm, size_t n, uint8_t* out, size_t cap) {
    const size_t need = AUDIO_HEAD_BYTES + 2 * n;
    if (cap < need) return 0;
    uint8_t* p = out;
    detail::put_header(p, T_AUDIO, gen, seq);
    detail::put(p, CODEC_PCM16_48K);
    std::memcpy(p, pcm, 2 * n);
    return need;
}

inline bool decode_header(const uint8_t* in, size_t len, Header& h) {
    if (len < HEADER_BYTES) return false;
    const uint8_t* p = in;
    detail::get(p, h.type); detail::get(p, h.ver); detail::get(p, h.gen); detail::get(p, h.seq);
    return h.ver == PROTO_VER;
}

inline bool decode_spectrum(const uint8_t* in, size_t len, Header& h, SpectrumFrame& f) {
    if (!decode_header(in, len, h) || h.type != T_SPECTRUM || len < SPECTRUM_HEAD_BYTES) return false;
    const uint8_t* p = in + HEADER_BYTES;
    detail::get(p, f.center_hz);
    detail::get(p, f.rate_hz);
    detail::get(p, f.n);
    detail::get(p, f.db_min);
    detail::get(p, f.db_step);
    if (f.n > sizeof(f.bins) || len != SPECTRUM_HEAD_BYTES + f.n) return false;
    f.gen = h.gen;
    std::memcpy(f.bins, p, f.n);
    return true;
}

inline bool decode_audio(const uint8_t* in, size_t len, Header& h, uint8_t& codec, int16_t* pcm, size_t cap, size_t& n) {
    if (!decode_header(in, len, h) || h.type != T_AUDIO || len < AUDIO_HEAD_BYTES) return false;
    codec = in[HEADER_BYTES];
    n = (len - AUDIO_HEAD_BYTES) / 2;
    if (n > cap) return false;
    std::memcpy(pcm, in + AUDIO_HEAD_BYTES, 2 * n);
    return true;
}

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
    return out;
}

inline std::string json_number(double v) {
    if (!std::isfinite(v)) return "null";
    char b[32];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

struct JsonWriter {
    std::string out;
    JsonWriter& begin_obj() { return open('{', '}'); }
    JsonWriter& begin_arr() { return open('[', ']'); }
    JsonWriter& end() { out += closers.back(); closers.pop_back(); firsts.pop_back(); return *this; }
    JsonWriter& key(const std::string& k) { sep(); out += '"'; out += json_escape(k); out += "\":"; pending_value = true; return *this; }
    JsonWriter& str(const std::string& v) { sep(); out += '"'; out += json_escape(v); out += '"'; return *this; }
    JsonWriter& num(double v) { sep(); out += json_number(v); return *this; }
    JsonWriter& boolean(bool v) { sep(); out += v ? "true" : "false"; return *this; }
    JsonWriter& raw(const std::string& v) { sep(); out += v.empty() ? "null" : v; return *this; }
private:
    std::vector<bool> firsts;
    std::vector<char> closers;
    bool pending_value = false;
    JsonWriter& open(char o, char c) { sep(); out += o; firsts.push_back(true); closers.push_back(c); return *this; }
    void sep() {
        if (pending_value) { pending_value = false; return; }
        if (firsts.empty()) return;
        if (!firsts.back()) out += ',';
        firsts.back() = false;
    }
};

using Fields = std::vector<std::pair<std::string, std::string>>;

namespace detail {
inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}
inline bool skip_value(const std::string& s, size_t& i, std::string& raw) {
    skip_ws(s, i);
    const size_t start = i;
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
        i++;
        while (i < s.size() && s[i] != '"') { if (s[i] == '\\') i++; i++; }
        if (i >= s.size()) return false;
        i++;
    } else if (c == '{' || c == '[') {
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '"') { std::string ignored; if (!skip_value(s, i, ignored)) return false; continue; }
            if (s[i] == '{' || s[i] == '[') depth++;
            else if (s[i] == '}' || s[i] == ']') { if (--depth == 0) { i++; break; } }
            i++;
        }
        if (depth != 0) return false;
    } else {
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') i++;
        if (i == start) return false;
    }
    raw = s.substr(start, i - start);
    return true;
}
}

inline std::string json_unescape(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"') return raw;
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 2 < raw.size()) {
            const char e = raw[++i];
            if (e == 'n') out += '\n'; else if (e == 't') out += '\t'; else if (e == 'r') out += '\r'; else out += e;
        } else out += raw[i];
    }
    return out;
}

// flat object: keys unescaped, values raw (strings keep their quotes); false on malformed input
inline bool json_parse_object(const std::string& s, Fields& fields) {
    fields.clear();
    size_t i = 0;
    detail::skip_ws(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    i++;
    detail::skip_ws(s, i);
    if (i < s.size() && s[i] == '}') return true;
    while (i < s.size()) {
        std::string key, value;
        if (!detail::skip_value(s, i, key) || key.empty() || key.front() != '"') return false;
        detail::skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        i++;
        if (!detail::skip_value(s, i, value)) return false;
        fields.emplace_back(json_unescape(key), value);
        detail::skip_ws(s, i);
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
    return false;
}

inline const std::string* json_find(const Fields& f, const char* key) {
    for (const auto& kv : f) if (kv.first == key) return &kv.second;
    return nullptr;
}

inline std::string json_str(const Fields& f, const char* key, const std::string& dflt = "") {
    const std::string* v = json_find(f, key);
    return v ? json_unescape(*v) : dflt;
}

inline double json_num(const Fields& f, const char* key, double dflt = 0.0) {
    const std::string* v = json_find(f, key);
    return v ? std::strtod(v->c_str(), nullptr) : dflt;
}

}
