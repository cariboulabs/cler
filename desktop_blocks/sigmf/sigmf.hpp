#pragma once

#include "cler_desktop_utils.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

// SigMF v1.x metadata: a <base>.sigmf-meta JSON file beside a <base>.sigmf-data
// raw sample file. Little-endian datatypes only.
namespace sigmf {

enum class Datatype { cf32_le, ci16_le, ci8, cu8, rf32_le, ri16_le };

inline const char* datatype_name(Datatype dt) {
    switch (dt) {
        case Datatype::cf32_le: return "cf32_le";
        case Datatype::ci16_le: return "ci16_le";
        case Datatype::ci8:     return "ci8";
        case Datatype::cu8:     return "cu8";
        case Datatype::rf32_le: return "rf32_le";
        case Datatype::ri16_le: return "ri16_le";
    }
    return "cf32_le";
}

inline bool datatype_is_complex(Datatype dt) {
    return dt == Datatype::cf32_le || dt == Datatype::ci16_le ||
           dt == Datatype::ci8 || dt == Datatype::cu8;
}

// bytes on disk per sample (a complex sample counts both components)
inline size_t datatype_size(Datatype dt) {
    switch (dt) {
        case Datatype::cf32_le: return 8;
        case Datatype::ci16_le: return 4;
        case Datatype::ci8:     return 2;
        case Datatype::cu8:     return 2;
        case Datatype::rf32_le: return 4;
        case Datatype::ri16_le: return 2;
    }
    return 8;
}

// SigMF core:datetime is ISO8601 in UTC, e.g. 2026-08-20T11:22:33.123Z
inline std::string utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count() % 1000;
    std::tm tm_utc;
    gmtime_r(&seconds, &tm_utc);
    char buf[40];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    std::snprintf(buf + n, sizeof(buf) - n, ".%03dZ", static_cast<int>(millis));
    return buf;
}

inline Datatype parse_datatype(const std::string& name) {
    // 8-bit types carry no meaningful byte order; the spec still allows the suffix
    if (name == "cf32_le") return Datatype::cf32_le;
    if (name == "ci16_le") return Datatype::ci16_le;
    if (name == "ci8" || name == "ci8_le") return Datatype::ci8;
    if (name == "cu8" || name == "cu8_le") return Datatype::cu8;
    if (name == "rf32_le") return Datatype::rf32_le;
    if (name == "ri16_le") return Datatype::ri16_le;
    std::string msg = "SigMF core:datatype not supported (little-endian only): " + name;
    cler::panic(msg.c_str());
    return Datatype::cf32_le;
}

// key -> raw JSON text, used to carry keys this reader does not model
using Fields = std::vector<std::pair<std::string, std::string>>;

struct Capture {
    uint64_t sample_start = 0;
    double frequency = 0.0;
    std::string datetime;
    bool has_frequency = false;
    Fields extra;
};

struct Meta {
    std::string version = "1.0.0";
    Datatype datatype = Datatype::cf32_le;
    double sample_rate = 0.0;
    std::string author;
    std::string description;
    std::string hw;
    Fields extra_global;
    std::vector<Capture> captures;
    std::vector<Fields> annotations;

    double center_frequency() const {
        for (const auto& c : captures) {
            if (c.has_frequency) return c.frequency;
        }
        return 0.0;
    }
};

namespace detail {

inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}

inline void json_fail(const char* what) {
    std::string msg = std::string("Malformed SigMF metadata JSON: ") + what;
    cler::panic(msg.c_str());
}

// consumes one JSON value and returns its raw text
inline std::string skip_value(const std::string& s, size_t& i) {
    skip_ws(s, i);
    size_t start = i;
    if (i >= s.size()) json_fail("unexpected end of input");
    char c = s[i];
    if (c == '"') {
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') i++;
            i++;
        }
        if (i >= s.size()) json_fail("unterminated string");
        i++;
    } else if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '"') {
                size_t j = i;
                skip_value(s, j);
                i = j;
                continue;
            }
            if (s[i] == c) depth++;
            else if (s[i] == close) {
                depth--;
                if (depth == 0) { i++; break; }
            }
            i++;
        }
        if (depth != 0) json_fail("unbalanced brackets");
    } else {
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
               s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') i++;
        if (i == start) json_fail("unexpected character");
    }
    return s.substr(start, i - start);
}

inline std::string unescape(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"') return raw;
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 2 < raw.size()) {
            char e = raw[++i];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'r') out += '\r';
            else out += e;
        } else {
            out += raw[i];
        }
    }
    return out;
}

inline std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

inline double as_number(const std::string& raw) { return std::strtod(raw.c_str(), nullptr); }

// parses "{...}" into its key/raw-value pairs, in file order
inline Fields parse_object(const std::string& raw) {
    Fields fields;
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '{') json_fail("expected an object");
    i++;
    skip_ws(raw, i);
    if (i < raw.size() && raw[i] == '}') return fields;
    while (i < raw.size()) {
        skip_ws(raw, i);
        std::string key = unescape(skip_value(raw, i));
        skip_ws(raw, i);
        if (i >= raw.size() || raw[i] != ':') json_fail("expected ':' after a key");
        i++;
        fields.emplace_back(key, skip_value(raw, i));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') { i++; continue; }
        break;
    }
    return fields;
}

inline std::vector<std::string> parse_array(const std::string& raw) {
    std::vector<std::string> items;
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '[') json_fail("expected an array");
    i++;
    skip_ws(raw, i);
    if (i < raw.size() && raw[i] == ']') return items;
    while (i < raw.size()) {
        items.push_back(skip_value(raw, i));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') { i++; continue; }
        break;
    }
    return items;
}

inline const std::string* find(const Fields& fields, const char* key) {
    for (const auto& kv : fields) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

inline void write_fields(std::string& out, const Fields& fields, const char* indent) {
    for (const auto& kv : fields) {
        out += ",\n";
        out += indent;
        out += "\"" + escape(kv.first) + "\": " + kv.second;
    }
}

inline std::string number_text(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

} // namespace detail

// derives the <base> from a base path or from either sidecar file's path
inline std::string base_path(const std::string& path) {
    const char* suffixes[] = {".sigmf-meta", ".sigmf-data"};
    for (const char* suffix : suffixes) {
        size_t n = std::string(suffix).size();
        if (path.size() > n && path.compare(path.size() - n, n, suffix) == 0) {
            return path.substr(0, path.size() - n);
        }
    }
    return path;
}

inline std::string meta_path(const std::string& path) { return base_path(path) + ".sigmf-meta"; }
inline std::string data_path(const std::string& path) { return base_path(path) + ".sigmf-data"; }

inline Meta read_meta(const std::string& path) {
    std::string file = meta_path(path);
    FILE* fp = std::fopen(file.c_str(), "rb");
    if (!fp) {
        std::string msg = "Failed to open SigMF metadata: " + file;
        cler::panic(msg.c_str());
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    std::fclose(fp);

    Meta meta;
    for (const auto& kv : detail::parse_object(text)) {
        if (kv.first == "global") {
            for (const auto& g : detail::parse_object(kv.second)) {
                if (g.first == "core:datatype") meta.datatype = parse_datatype(detail::unescape(g.second));
                else if (g.first == "core:sample_rate") meta.sample_rate = detail::as_number(g.second);
                else if (g.first == "core:version") meta.version = detail::unescape(g.second);
                else if (g.first == "core:author") meta.author = detail::unescape(g.second);
                else if (g.first == "core:description") meta.description = detail::unescape(g.second);
                else if (g.first == "core:hw") meta.hw = detail::unescape(g.second);
                else meta.extra_global.push_back(g);
            }
        } else if (kv.first == "captures") {
            for (const auto& item : detail::parse_array(kv.second)) {
                Capture capture;
                for (const auto& c : detail::parse_object(item)) {
                    if (c.first == "core:sample_start") {
                        capture.sample_start = static_cast<uint64_t>(detail::as_number(c.second));
                    } else if (c.first == "core:frequency") {
                        capture.frequency = detail::as_number(c.second);
                        capture.has_frequency = true;
                    } else if (c.first == "core:datetime") {
                        capture.datetime = detail::unescape(c.second);
                    } else {
                        capture.extra.push_back(c);
                    }
                }
                meta.captures.push_back(std::move(capture));
            }
        } else if (kv.first == "annotations") {
            for (const auto& item : detail::parse_array(kv.second)) {
                meta.annotations.push_back(detail::parse_object(item));
            }
        }
    }
    if (meta.captures.empty()) meta.captures.push_back(Capture{});
    return meta;
}

inline std::string to_json(const Meta& meta) {
    std::string out = "{\n  \"global\": {\n";
    out += "    \"core:datatype\": \"" + std::string(datatype_name(meta.datatype)) + "\"";
    out += ",\n    \"core:sample_rate\": " + detail::number_text(meta.sample_rate);
    out += ",\n    \"core:version\": \"" + detail::escape(meta.version) + "\"";
    if (!meta.author.empty()) out += ",\n    \"core:author\": \"" + detail::escape(meta.author) + "\"";
    if (!meta.description.empty()) out += ",\n    \"core:description\": \"" + detail::escape(meta.description) + "\"";
    if (!meta.hw.empty()) out += ",\n    \"core:hw\": \"" + detail::escape(meta.hw) + "\"";
    detail::write_fields(out, meta.extra_global, "    ");
    out += "\n  },\n  \"captures\": [";

    for (size_t i = 0; i < meta.captures.size(); ++i) {
        const Capture& capture = meta.captures[i];
        out += (i == 0) ? "\n" : ",\n";
        out += "    {\n      \"core:sample_start\": " + std::to_string(capture.sample_start);
        if (capture.has_frequency) out += ",\n      \"core:frequency\": " + detail::number_text(capture.frequency);
        if (!capture.datetime.empty()) out += ",\n      \"core:datetime\": \"" + detail::escape(capture.datetime) + "\"";
        detail::write_fields(out, capture.extra, "      ");
        out += "\n    }";
    }
    out += meta.captures.empty() ? "],\n  \"annotations\": [" : "\n  ],\n  \"annotations\": [";

    for (size_t i = 0; i < meta.annotations.size(); ++i) {
        out += (i == 0) ? "\n" : ",\n";
        out += "    {";
        const Fields& annotation = meta.annotations[i];
        for (size_t k = 0; k < annotation.size(); ++k) {
            out += (k == 0) ? "\n      " : ",\n      ";
            out += "\"" + detail::escape(annotation[k].first) + "\": " + annotation[k].second;
        }
        out += "\n    }";
    }
    out += meta.annotations.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

inline bool write_meta(const std::string& path, const Meta& meta) {
    std::string file = meta_path(path);
    FILE* fp = std::fopen(file.c_str(), "wb");
    if (!fp) return false;
    std::string text = to_json(meta);
    bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
    std::fclose(fp);
    return ok;
}

// {"core:sample_start": .., "core:sample_count": .., "core:label": ".."}
inline Fields make_annotation(uint64_t sample_start, uint64_t sample_count, const std::string& label) {
    Fields annotation;
    annotation.emplace_back("core:sample_start", std::to_string(sample_start));
    annotation.emplace_back("core:sample_count", std::to_string(sample_count));
    if (!label.empty()) annotation.emplace_back("core:label", "\"" + detail::escape(label) + "\"");
    return annotation;
}

} // namespace sigmf
