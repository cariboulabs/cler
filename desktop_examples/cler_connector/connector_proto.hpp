#pragma once

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Wire-level parsing for the owrx_connector protocol: argv, the gain spec
// grammar, and the "<key>:<value>\n" control stream. No sockets, no devices.
namespace conn {

struct Options {
    int iq_port = 4950;
    int control_port = -1;
    int rtltcp_port = -1;
    double samp_rate = 0;
    double frequency = 0;
    double ppm = 0;
    std::string device;
    std::string gain = "auto";
    std::string antenna;
    std::string settings;
    bool iqswap = false;
    bool help = false;
    bool version = false;
    std::string error;
};

struct GainSpec {
    bool automatic = true;
    bool single = false;
    double value = 0;
    std::vector<std::pair<std::string, double>> pairs;
};

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline bool parse_number(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

// "auto"|"none"|"None"|"" -> AGC, a bare number -> one gain, else "K=V,K=V".
inline GainSpec parse_gain(const std::string& spec) {
    GainSpec g;
    const std::string l = lower(spec);
    if (l.empty() || l == "auto" || l == "none") return g;
    double v = 0;
    if (parse_number(spec, v)) {
        g.automatic = false;
        g.single = true;
        g.value = v;
        return g;
    }
    size_t at = 0;
    while (at <= spec.size()) {
        const size_t comma = spec.find(',', at);
        const std::string item = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && parse_number(item.substr(eq + 1), v)) {
            g.pairs.emplace_back(item.substr(0, eq), v);
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    g.automatic = g.pairs.empty();
    return g;
}

inline Options parse_args(int argc, char* const* argv) {
    Options o;
    auto want = [&](int& i, const char* name) -> const char* {
        if (i + 1 >= argc) { o.error = std::string("missing value for ") + name; return nullptr; }
        return argv[++i];
    };
    for (int i = 1; i < argc && o.error.empty(); ++i) {
        std::string a = argv[i];
        // OWRX passes "-p 4950"; "--port=4950" costs one split and saves a support ticket.
        std::string inline_value;
        bool has_inline = false;
        if (a.rfind("--", 0) == 0) {
            const size_t eq = a.find('=');
            if (eq != std::string::npos) {
                inline_value = a.substr(eq + 1);
                a = a.substr(0, eq);
                has_inline = true;
            }
        }
        auto value = [&](const char* name) -> const char* {
            return has_inline ? inline_value.c_str() : want(i, name);
        };
        // A silently mis-parsed port binds somewhere OWRX will never connect to.
        auto number = [&](const char* name, double& out) {
            const char* v = value(name);
            if (v && !parse_number(v, out)) o.error = std::string(name) + " wants a number, got '" + v + "'";
        };
        auto integer = [&](const char* name, int& out) {
            double v = 0;
            number(name, v);
            if (o.error.empty()) out = static_cast<int>(v);
        };
        if (a == "-p" || a == "--port") integer("--port", o.iq_port);
        else if (a == "-c" || a == "--control") integer("--control", o.control_port);
        else if (a == "-r" || a == "--rtltcp") integer("--rtltcp", o.rtltcp_port);
        else if (a == "-s" || a == "--samplerate") number("--samplerate", o.samp_rate);
        else if (a == "-f" || a == "--frequency") number("--frequency", o.frequency);
        else if (a == "-P" || a == "--ppm") number("--ppm", o.ppm);
        else if (a == "-d" || a == "--device") { if (const char* v = value("--device")) o.device = v; }
        else if (a == "-g" || a == "--gain") { if (const char* v = value("--gain")) o.gain = v; }
        else if (a == "-a" || a == "--antenna") { if (const char* v = value("--antenna")) o.antenna = v; }
        else if (a == "-t" || a == "--settings") { if (const char* v = value("--settings")) o.settings = v; }
        else if (a == "-i" || a == "--iqswap") o.iqswap = true;
        else if (a == "-h" || a == "--help") o.help = true;
        else if (a == "-v" || a == "--version") o.version = true;
        else o.error = "unknown argument " + a;
    }
    return o;
}

// OWRX reads in 256-byte chunks, so a line can arrive in pieces; a client that
// never sends a newline must not grow the buffer without bound.
struct LineReader {
    static constexpr size_t MAX_LINE = 64 * 1024;
    std::string buf;
    bool overflowed = false;

    template <typename F>
    void feed(const char* data, size_t n, F&& on_line) {
        for (size_t i = 0; i < n; ++i) {
            const char c = data[i];
            if (c == '\n') {
                if (!overflowed) {
                    if (!buf.empty() && buf.back() == '\r') buf.pop_back();
                    if (!buf.empty()) on_line(buf);
                }
                buf.clear();
                overflowed = false;
            } else if (buf.size() >= MAX_LINE) {
                overflowed = true;
                buf.clear();
            } else {
                buf += c;
            }
        }
    }
};

inline bool split_kv(const std::string& line, std::string& key, std::string& value) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    key = line.substr(0, colon);
    value = line.substr(colon + 1);
    return true;
}

inline bool truthy(const std::string& v) {
    const std::string l = lower(v);
    return l == "true" || l == "1";
}

inline std::string basename_of(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

}  // namespace conn
