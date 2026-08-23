// Browser receiver: SourceMux -> fanout -> {spectrum, shift -> resampler -> demod} -> WebSink.
//   ./websdr [--source sim|hackrf[:serial]] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]
//            [--gain NAME=V]... [--port N] [--bind ADDR] [--token T] [--client-dir DIR]
//            [--record-dir DIR] [--state-file FILE] [--version]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_mux.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/spectrum/spectrum.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/demod/analog_demod.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/web_sink.hpp"
#include "websdr_client_files.hpp"

#include <sys/statvfs.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WEBSDR_VERSION
#define WEBSDR_VERSION "dev"
#endif

static std::atomic<bool> g_run{true};

static constexpr double CHANNEL_RATE = 240e3;
static constexpr double CHANNEL_BW = 240e3;
static constexpr double IF_OFFSET = 400e3;   // keeps the SDR's DC spike out of the channel

static AnalogDemodBlock::Mode parse_mode(const std::string& m) {
    if (m == "NBFM") return AnalogDemodBlock::Mode::NBFM;
    if (m == "AM") return AnalogDemodBlock::Mode::AM;
    if (m == "USB") return AnalogDemodBlock::Mode::USB;
    if (m == "LSB") return AnalogDemodBlock::Mode::LSB;
    return AnalogDemodBlock::Mode::WBFM;
}

static double passband_for(AnalogDemodBlock::Mode m) {
    switch (m) {
        case AnalogDemodBlock::Mode::WBFM: return 200e3;
        case AnalogDemodBlock::Mode::NBFM: return 12.5e3;
        case AnalogDemodBlock::Mode::AM: return 10e3;
        default: return 3.2e3;
    }
}

static bool parse_source(const std::string& s, SourceMux::Kind& kind, std::string& id) {
    const size_t c = s.find(':');
    const std::string k = s.substr(0, c);
    id = c == std::string::npos ? "" : s.substr(c + 1);
    if (k == "sim") { kind = SourceMux::Kind::Sim; return true; }
    if (k == "hackrf") { kind = SourceMux::Kind::HackRF; return true; }
    return false;
}

static std::string source_id(SourceMux::Kind kind, const std::string& id) {
    std::string s = SourceMux::kind_name(kind);
    if (!id.empty()) s += ":" + id;
    return s;
}

static double free_disk(const std::string& dir) {
    struct statvfs st;
    if (statvfs(dir.empty() ? "." : dir.c_str(), &st) != 0) return 0.0;
    return static_cast<double>(st.f_bavail) * static_cast<double>(st.f_frsize);
}

struct App {
    SourceMux src{"Source"};
    FanoutBlock<std::complex<float>> fan{"RF fanout", 2, 1 << 20};
    SpectrumBlock spec;
    FrequencyShiftBlock shift;
    MultiStageResamplerBlock<std::complex<float>> resamp;
    AnalogDemodBlock demod;
    web::WebServer& srv;
    WebSinkBlock sink;

    double rate, center = 100e6, offset = 0.0;
    AnalogDemodBlock::Mode mode = AnalogDemodBlock::Mode::WBFM;
    uint32_t gen = 0;
    bool running = false, switching = false, lost = false;
    SourceMux::Kind kind = SourceMux::Kind::None;
    std::string id, state_file;
    std::vector<std::pair<std::string, double>> gains;

    App(web::WebServer& server, double rate_hz, double freq_hz, AnalogDemodBlock::Mode m)
        : spec("Spectrum", rate_hz, 1024, 20.0f),
          shift("Tune shift", 0.0, rate_hz, 1 << 18),
          resamp("Channel", static_cast<float>(CHANNEL_RATE / rate_hz), 60.0f, 1 << 18),
          demod("Demod", CHANNEL_RATE, m, 1 << 16),
          srv(server), sink("Web sink", server),
          rate(rate_hz), center(freq_hz), mode(m) {}

    double tuned() const { return center + offset; }

    std::string state_json() {
        web::JsonWriter w;
        w.begin_obj().key("gen").num(gen).key("source").str(running ? source_id(kind, id) : "")
         .key("freq").num(tuned()).key("center").num(center).key("offset").num(offset)
         .key("passband").num(passband_for(mode)).key("rate").num(rate)
         .key("mode").str(AnalogDemodBlock::mode_name(mode))
         .key("switching").boolean(switching).key("source_lost").boolean(lost)
         .key("recording").boolean(false);
        for (const auto& c : src.capabilities()) {
            if (c.id == "freq" || c.id == "rate") continue;
            w.key(c.id).num(c.value);
        }
        w.end();
        return w.out;
    }

    std::string hello_json() {
        web::JsonWriter w;
        w.begin_obj().key("sources").begin_arr();
        for (const auto& d : src.enumerate()) {
            w.begin_obj().key("id").str(source_id(d.kind, d.id)).key("kind").str(SourceMux::kind_name(d.kind))
             .key("label").str(d.label).key("available").boolean(true).end();
        }
        w.end().key("controls").begin_arr();
        w.begin_obj().key("id").str("mode").key("label").str("mode").key("type").str("enum").key("options").begin_arr();
        for (const char* m : {"WBFM", "NBFM", "AM", "USB", "LSB"}) w.str(m);
        w.end().end();
        for (const auto& c : src.capabilities()) {
            w.begin_obj().key("id").str(c.id).key("label").str(c.label).key("type").str(c.type);
            if (!c.unit.empty()) w.key("unit").str(c.unit);
            if (c.type == "enum") { w.key("options").begin_arr(); for (const auto& o : c.options) w.str(o); w.end(); }
            else { w.key("min").num(c.min).key("max").num(c.max).key("step").num(c.step); }
            if (c.ro) w.key("ro").boolean(true);
            w.end();
        }
        w.end().key("spectrum").begin_obj().key("n").num(1024).key("fps").num(20).end().end();
        return w.out.substr(1, w.out.size() - 2);
    }

    void publish() {
        ++gen;
        srv.set_gen(gen);
        spec.set_gen(gen);
        spec.set_center(center);
        srv.set_hello_extra(hello_json());
        srv.set_state(state_json());
        web::JsonWriter h;
        h.begin_obj().key("source").str(running ? source_id(kind, id) : "").key("rate").num(rate)
         .key("overflows").num(static_cast<double>(src.overflows())).key("recording").boolean(false)
         .key("free_disk").num(free_disk(record_dir)).end();
        srv.set_health_extra(h.out.substr(1, h.out.size() - 2));
        save_state();
    }

    std::string record_dir;

    template <typename FG>
    bool select(FG& fg, SourceMux::Kind k, const std::string& dev, double freq_hz, double rate_hz) {
        switching = true;
        srv.set_state(state_json());
        if (running) { fg.stop(); running = false; }
        const bool ok = src.select(k, dev, freq_hz, rate_hz);
        switching = false;
        kind = k; id = dev;
        if (!ok) {
            srv.send_error("source", "could not open " + source_id(k, dev));
            publish();
            return false;
        }
        rate = src.rate(); center = src.center(); offset = 0.0; lost = false;
        for (const auto& g : gains) src.set(g.first, g.second);
        resamp.set_ratio(static_cast<float>(CHANNEL_RATE / rate));
        spec.set_rate(rate);
        shift.set_sample_rate(rate);
        shift.set_frequency_shift(0.0);
        fg.reset();
        fg.run();
        running = true;
        publish();
        return true;
    }

    void tune_offset(double hz) {
        if (std::fabs(hz) + CHANNEL_BW / 2.0 > rate / 2.0) { tune_to(center + hz); return; }
        offset = hz;
        shift.set_frequency_shift(-offset);
    }

    void tune_to(double hz) {
        center = std::clamp(hz - IF_OFFSET, 1e6, 6e9);
        src.set("freq", center);
        const double back = hz - center;
        if (std::fabs(back) + CHANNEL_BW / 2.0 <= rate / 2.0) {
            offset = back;
            shift.set_frequency_shift(-offset);
        }
    }

    void save_state() {
        if (state_file.empty()) return;
        web::JsonWriter w;
        w.begin_obj().key("source").str(source_id(kind, id)).key("freq").num(tuned()).key("rate").num(rate)
         .key("mode").str(AnalogDemodBlock::mode_name(mode));
        for (const auto& c : src.capabilities()) if (!c.ro && c.id != "freq") w.key(c.id).num(c.value);
        w.end();
        std::ofstream(state_file) << w.out;
    }
};

int main(int argc, char** argv) {
    web::ServerOptions o;
    o.files = WEBSDR_CLIENT_FILES;
    o.file_count = WEBSDR_CLIENT_FILES_COUNT;
    o.version = WEBSDR_VERSION;
    std::string source, record_dir, state_file, mode_s = "WBFM";
    double freq = 100e6, rate = 2.4e6;
    std::vector<std::pair<std::string, double>> gains;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> const char* { return (!std::strcmp(argv[i], flag) && i + 1 < argc) ? argv[++i] : nullptr; };
        if (!std::strcmp(argv[i], "--version")) { std::printf("websdr %s\n", WEBSDR_VERSION); return 0; }
        else if (const char* v = next("--source")) source = v;
        else if (const char* v = next("--freq")) freq = std::atof(v);
        else if (const char* v = next("--rate")) rate = std::atof(v);
        else if (const char* v = next("--mode")) mode_s = v;
        else if (const char* v = next("--gain")) {
            const char* eq = std::strchr(v, '=');
            if (!eq) { std::fprintf(stderr, "--gain NAME=V\n"); return 1; }
            gains.emplace_back(std::string(v, eq), std::atof(eq + 1));
        }
        else if (const char* v = next("--port")) o.port = std::atoi(v);
        else if (const char* v = next("--bind")) o.bind = v;
        else if (const char* v = next("--token")) o.token = v;
        else if (const char* v = next("--client-dir")) o.client_dir = v;
        else if (const char* v = next("--record-dir")) record_dir = v;
        else if (const char* v = next("--state-file")) state_file = v;
        else {
            std::fprintf(stderr, "Usage: %s [--source sim|hackrf[:serial]] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]\n"
                                 "          [--gain NAME=V]... [--port N] [--bind ADDR] [--token T] [--client-dir DIR]\n"
                                 "          [--record-dir DIR] [--state-file FILE] [--version]\n", argv[0]);
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }
    if (!state_file.empty()) {
        std::ifstream f(state_file);
        std::stringstream ss; ss << f.rdbuf();
        web::Fields fields;
        if (f && web::json_parse_object(ss.str(), fields)) {
            if (source.empty()) source = web::json_str(fields, "source");
            freq = web::json_num(fields, "freq", freq);
            rate = web::json_num(fields, "rate", rate);
            mode_s = web::json_str(fields, "mode", mode_s);
            for (const auto& [k, v] : fields)
                if (k != "source" && k != "freq" && k != "rate" && k != "mode") gains.emplace_back(k, std::atof(v.c_str()));
        }
    }
    std::signal(SIGINT, [](int) { g_run = false; });
    std::signal(SIGTERM, [](int) { g_run = false; });

    web::WebServer srv(o);
    App app(srv, rate, freq, parse_mode(mode_s));
    app.gains = gains;
    app.record_dir = record_dir;
    app.state_file = state_file;

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&app.src, &app.fan.in),
        cler::BlockRunner(&app.fan, &app.spec.in, &app.shift.in),
        cler::BlockRunner(&app.spec, &app.sink.spectrum),
        cler::BlockRunner(&app.shift, &app.resamp.in),
        cler::BlockRunner(&app.resamp, &app.demod.in),
        cler::BlockRunner(&app.demod, &app.sink.audio),
        cler::BlockRunner(&app.sink));

    app.publish();
    srv.start();
    std::printf("websdr %s on http://%s:%d/\n", WEBSDR_VERSION, o.bind.c_str(), o.port);

    SourceMux::Kind k; std::string id;
    if (!source.empty() && source != "none") {
        if (!parse_source(source, k, id)) { std::fprintf(stderr, "unknown --source %s\n", source.c_str()); return 1; }
        app.select(fg, k, id, freq, rate);
    } else if (source.empty()) {
        std::vector<SourceMux::DeviceInfo> real;
        for (const auto& d : app.src.enumerate()) if (d.kind != SourceMux::Kind::Sim) real.push_back(d);
        if (real.size() == 1) app.select(fg, real[0].kind, real[0].id, freq, rate);
    }

    using clock = std::chrono::steady_clock;
    auto last_stats = clock::now(), last_retry = clock::now();
    while (g_run) {
        std::string ctl;
        while (srv.pop_control(ctl)) {
            web::Fields f;
            if (!web::json_parse_object(ctl, f)) continue;
            const std::string t = web::json_str(f, "t");
            if (t == "set") {
                if (web::json_find(f, "freq")) app.tune_to(web::json_num(f, "freq"));
                if (web::json_find(f, "offset")) app.tune_offset(web::json_num(f, "offset"));
                if (web::json_find(f, "mode")) { app.mode = parse_mode(web::json_str(f, "mode")); app.demod.set_mode(app.mode); }
                for (const auto& [key, val] : f) {
                    if (key == "t" || key == "freq" || key == "offset" || key == "mode" || key == "rate") continue;
                    const double v = val == "true" ? 1.0 : val == "false" ? 0.0 : std::atof(val.c_str());
                    app.src.set(key, v);
                    bool seen = false;
                    for (auto& g : app.gains) if (g.first == key) { g.second = v; seen = true; }
                    if (!seen) app.gains.emplace_back(key, v);
                }
                app.publish();
            } else if (t == "source") {
                if (parse_source(web::json_str(f, "id"), k, id)) { app.lost = false; app.select(fg, k, id, app.tuned(), app.rate); }
                else srv.send_error("source", "unknown source");
            } else if (t == "rescan") {
                app.publish();
            }
        }
        const auto now = clock::now();
        if (now - last_stats >= std::chrono::seconds(1)) {
            last_stats = now;
            if (app.running && app.src.lost() && !app.lost) { app.lost = true; app.publish(); }
            web::JsonWriter w;
            w.begin_obj().key("overflows").num(static_cast<double>(app.src.overflows())).key("source_lost").boolean(app.lost).end();
            srv.set_stats_extra(w.out.substr(1, w.out.size() - 2));
        }
        if (app.lost && now - last_retry >= std::chrono::seconds(2)) {
            last_retry = now;
            app.select(fg, app.kind, app.id, app.tuned(), app.rate);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (app.running) fg.stop();
    srv.stop();
    return 0;
}
