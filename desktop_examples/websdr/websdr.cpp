// Browser receiver: SourceMux -> fanout -> {spectrum, shift -> resampler -> demod} -> WebSink.
//   ./websdr [--source sim|hackrf[:serial]|none] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]
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

#include "websdr_version.hpp"
#ifndef WEBSDR_VERSION
#define WEBSDR_VERSION "dev"
#endif

static std::atomic<bool> g_run{true};

static constexpr double CHANNEL_HZ = 240e3;    // demod input rate and channel width
static constexpr double IF_OFFSET = 400e3;     // keeps the SDR's DC spike out of the channel

static bool parse_mode(const std::string& m, AnalogDemodBlock::Mode& out) {
    for (auto mode : {AnalogDemodBlock::Mode::WBFM, AnalogDemodBlock::Mode::NBFM, AnalogDemodBlock::Mode::AM,
                      AnalogDemodBlock::Mode::USB, AnalogDemodBlock::Mode::LSB}) {
        if (m == AnalogDemodBlock::mode_name(mode)) { out = mode; return true; }
    }
    return false;
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
    for (auto kk : {SourceMux::Kind::HackRF, SourceMux::Kind::Pluto, SourceMux::Kind::UHD,
                    SourceMux::Kind::Cariboulite, SourceMux::Kind::Soapy, SourceMux::Kind::SigMF,
                    SourceMux::Kind::Sim}) {
        if (k == SourceMux::kind_name(kk)) { kind = kk; return true; }
    }
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
    SourceMux& src;
    FanoutBlock<std::complex<float>>& fan;
    SpectrumBlock& spec;
    FrequencyShiftBlock& shift;
    MultiStageResamplerBlock<std::complex<float>>& resamp;
    AnalogDemodBlock& demod;
    web::WebServer& srv;
    WebSinkBlock& sink;

    std::string record_dir, state_file;
    std::vector<std::pair<std::string, double>> gains;
    double rate, center, offset = 0.0;
    AnalogDemodBlock::Mode mode;
    uint32_t gen = 0;
    bool running = false, switching = false, lost = false;
    SourceMux::Kind kind = SourceMux::Kind::None;
    std::string id;
    std::vector<SourceMux::DeviceInfo> devices;
    std::vector<SourceMux::Control> caps;

    App(SourceMux& s, FanoutBlock<std::complex<float>>& f, SpectrumBlock& sp, FrequencyShiftBlock& sh,
        MultiStageResamplerBlock<std::complex<float>>& r, AnalogDemodBlock& d, web::WebServer& server,
        WebSinkBlock& k, double rate_hz, double freq_hz, AnalogDemodBlock::Mode m)
        : src(s), fan(f), spec(sp), shift(sh), resamp(r), demod(d), srv(server), sink(k),
          rate(rate_hz), center(freq_hz), mode(m) {}

    double tuned() const { return center + offset; }
    std::string source() const { return running ? source_id(kind, id) : ""; }

    void rescan() { devices = src.enumerate(); caps = src.capabilities(); }

    void bump_gen() {
        ++gen;
        srv.set_gen(gen);
        spec.set_gen(gen);
        spec.set_center(center);
    }

    std::string state_json() {
        web::JsonWriter w;
        w.begin_obj().key("gen").num(gen).key("source").str(source())
         .key("freq").num(tuned()).key("center").num(center).key("offset").num(offset)
         .key("passband").num(passband_for(mode)).key("rate").num(rate)
         .key("mode").str(AnalogDemodBlock::mode_name(mode))
         .key("switching").boolean(switching).key("source_lost").boolean(lost)
         .key("recording").boolean(false);
        for (const auto& c : caps) {
            if (c.id == "freq" || c.id == "rate") continue;
            w.key(c.id).num(c.value);
        }
        w.end();
        return w.out;
    }

    std::string hello_json() {
        web::JsonWriter w;
        w.begin_obj().key("sources").begin_arr();
        for (const auto& d : devices) {
            w.begin_obj().key("id").str(source_id(d.kind, d.id)).key("kind").str(SourceMux::kind_name(d.kind))
             .key("label").str(d.label).key("available").boolean(true).end();
        }
        w.end().key("controls").begin_arr();
        w.begin_obj().key("id").str("mode").key("label").str("mode").key("type").str("enum").key("options").begin_arr();
        for (const char* m : {"WBFM", "NBFM", "AM", "USB", "LSB"}) w.str(m);
        w.end().end();
        for (const auto& c : caps) {
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

    // state to every tab; hello too when the device list or controls changed
    void publish(bool hello = false) {
        caps = src.capabilities();
        srv.set_state(state_json());
        if (hello) { srv.set_hello_extra(hello_json()); srv.resend_hello(); }
        web::JsonWriter h;
        h.begin_obj().key("source").str(source()).key("rate").num(rate)
         .key("overflows").num(static_cast<double>(src.overflows())).key("recording").boolean(false)
         .key("free_disk").num(free_disk(record_dir)).end();
        srv.set_health_extra(h.out.substr(1, h.out.size() - 2));
        if (running) save_state();
    }

    template <typename FG>
    bool select(FG& fg, SourceMux::Kind k, const std::string& dev, double freq_hz, double rate_hz, bool quiet = false) {
        switching = true;
        srv.set_state(state_json());
        if (running) { fg.stop(); running = false; }
        const bool ok = src.select(k, dev, freq_hz, rate_hz);
        switching = false;
        kind = k; id = dev;
        rescan();
        if (!ok) {
            if (!quiet) { srv.send_error("source", "could not open " + source_id(k, dev)); publish(true); }
            return false;
        }
        rate = src.rate(); center = src.center(); offset = 0.0; lost = false;
        if (std::fabs(center - freq_hz) > rate) freq_hz = center + IF_OFFSET;
        for (const auto& g : gains) src.set(g.first, g.second);
        resamp.set_ratio(static_cast<float>(CHANNEL_HZ / rate));
        spec.set_rate(rate);
        shift.set_sample_rate(rate);
        tune_to(freq_hz);
        fg.reset();
        fg.run();
        running = true;
        publish(true);
        return true;
    }

    bool fits(double off) const {
        return std::fabs(off) + CHANNEL_HZ / 2.0 <= rate / 2.0 && std::fabs(off) >= CHANNEL_HZ / 2.0 + 20e3;
    }

    void tune_offset(double hz) {
        if (!fits(hz)) { tune_to(center + hz); return; }
        offset = hz;
        shift.set_frequency_shift(-offset);
        bump_gen();
    }

    void tune_to(double hz) {
        if (rate < 2 * IF_OFFSET + CHANNEL_HZ) {
            center = std::clamp(hz, 1e6, 6e9);
            offset = 0.0;
        } else if (fits(hz - center)) {
            offset = hz - center;
        } else {
            center = std::clamp(hz - IF_OFFSET, 1e6, 6e9);
            offset = hz - center;
        }
        src.set("freq", center);
        center = src.center();
        shift.set_frequency_shift(-offset);
        bump_gen();
    }

    bool set_control(const std::string& key, double v) {
        for (const auto& c : caps) {
            if (c.id != key) continue;
            if (c.ro) return false;
            src.set(key, v);
            for (auto& g : gains) if (g.first == key) { g.second = v; return true; }
            gains.emplace_back(key, v);
            return true;
        }
        return false;
    }

    void save_state() {
        if (state_file.empty()) return;
        web::JsonWriter w;
        w.begin_obj().key("source").str(source_id(kind, id)).key("freq").num(tuned()).key("rate").num(rate)
         .key("mode").str(AnalogDemodBlock::mode_name(mode));
        for (const auto& c : caps) if (!c.ro && c.id != "freq") w.key(c.id).num(c.value);
        w.end();
        std::ofstream(state_file) << w.out;
    }
};

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    web::ServerOptions o;
    o.files = WEBSDR_CLIENT_FILES;
    o.file_count = WEBSDR_CLIENT_FILES_COUNT;
    o.version = WEBSDR_VERSION;
    std::string source, record_dir, state_file, mode_s;
    double freq = 0, rate = 0;
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
            std::fprintf(stderr, "Usage: %s [--source sim|hackrf[:serial]|none] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]\n"
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
            if (freq == 0) freq = web::json_num(fields, "freq");
            if (rate == 0) rate = web::json_num(fields, "rate");
            if (mode_s.empty()) mode_s = web::json_str(fields, "mode");
            for (const auto& [k, v] : fields) {
                if (k == "source" || k == "freq" || k == "rate" || k == "mode") continue;
                bool given = false;
                for (const auto& g : gains) given = given || g.first == k;
                if (!given) gains.emplace_back(k, std::atof(v.c_str()));
            }
        }
    }
    if (freq == 0) freq = 100e6;
    if (rate == 0) rate = 2.4e6;
    if (mode_s.empty()) mode_s = "WBFM";
    AnalogDemodBlock::Mode mode;
    if (!parse_mode(mode_s, mode)) { std::fprintf(stderr, "unknown --mode %s\n", mode_s.c_str()); return 1; }
    SourceMux::Kind k = SourceMux::Kind::None; std::string id;
    if (!source.empty() && source != "none" && !parse_source(source, k, id)) {
        std::fprintf(stderr, "unknown --source %s\n", source.c_str());
        return 1;
    }
    std::signal(SIGINT, [](int) { g_run = false; });
    std::signal(SIGTERM, [](int) { g_run = false; });

    const bool loopback = o.bind == "127.0.0.1" || o.bind == "localhost" || o.bind == "::1";
    if (!loopback && o.token.empty()) {
        unsigned char rnd[12];
        FILE* ur = std::fopen("/dev/urandom", "rb");
        if (!ur || std::fread(rnd, 1, sizeof rnd, ur) != sizeof rnd) cler::panic("websdr: /dev/urandom");
        std::fclose(ur);
        char hex[sizeof rnd * 2 + 1];
        for (size_t i = 0; i < sizeof rnd; ++i) std::snprintf(hex + 2 * i, 3, "%02x", rnd[i]);
        o.token = hex;
    }
    web::WebServer srv(o);
    SourceMux src("Source");
    FanoutBlock<std::complex<float>> fan("RF fanout", 2, 1 << 20);
    SpectrumBlock spec("Spectrum", rate, 1024, 20.0f, -120.0f, 0.5f, 4, SpectralWindow::Hann, 1 << 16);
    FrequencyShiftBlock shift("Tune shift", 0.0, rate, 1 << 18);
    MultiStageResamplerBlock<std::complex<float>> resamp("Channel", static_cast<float>(CHANNEL_HZ / rate), 60.0f, 1 << 18);
    AnalogDemodBlock demod("Demod", CHANNEL_HZ, mode, 1 << 16);
    WebSinkBlock sink("Web sink", srv);
    App app(src, fan, spec, shift, resamp, demod, srv, sink, rate, freq, mode);
    app.gains = gains;
    app.record_dir = record_dir;
    app.state_file = state_file;

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &fan.in),
        cler::BlockRunner(&fan, &spec.in, &shift.in),
        cler::BlockRunner(&spec, &sink.spectrum),
        cler::BlockRunner(&shift, &resamp.in),
        cler::BlockRunner(&resamp, &demod.in),
        cler::BlockRunner(&demod, &sink.audio),
        cler::BlockRunner(&sink));

    app.rescan();
    app.publish(true);
    srv.start();
    if (o.token.empty()) std::printf("websdr %s on http://%s:%d/\n", WEBSDR_VERSION, o.bind.c_str(), o.port);
    else std::printf("websdr %s on http://%s:%d/?token=%s\n", WEBSDR_VERSION, o.bind.c_str(), o.port, o.token.c_str());

    const bool auto_pick = source.empty();
    auto pick_only_device = [&]() {
        std::vector<SourceMux::DeviceInfo> real;
        for (const auto& d : app.devices) if (d.kind != SourceMux::Kind::Sim) real.push_back(d);
        if (real.size() == 1) app.select(fg, real[0].kind, real[0].id, freq, rate);
    };
    if (k != SourceMux::Kind::None) app.select(fg, k, id, freq, rate);
    else if (auto_pick) pick_only_device();

    using clock = std::chrono::steady_clock;
    auto last_stats = clock::now(), last_retry = clock::now();
    while (g_run) {
        std::string ctl;
        while (srv.pop_control(ctl)) {
            web::Fields f;
            if (!web::json_parse_object(ctl, f)) continue;
            const std::string t = web::json_str(f, "t");
            if (t == "set") {
                for (const auto& [key, val] : f) {
                    if (key == "t") continue;
                    const double v = val == "true" ? 1.0 : val == "false" ? 0.0 : std::atof(val.c_str());
                    if (key == "freq") app.tune_to(v);
                    else if (key == "offset") app.tune_offset(v);
                    else if (key == "mode") {
                        AnalogDemodBlock::Mode m;
                        if (parse_mode(web::json_str(f, "mode"), m)) { app.mode = m; app.demod.set_mode(m); }
                        else srv.send_error("set", "unknown mode");
                    }
                    else if (!app.set_control(key, v)) srv.send_error("set", "unknown or read-only control " + key);
                }
                app.publish();
            } else if (t == "source") {
                if (parse_source(web::json_str(f, "id"), k, id)) app.select(fg, k, id, app.tuned(), app.rate);
                else srv.send_error("source", "unknown source");
            } else if (t == "rescan") {
                app.rescan();
                app.publish(true);
            }
        }
        const auto now = clock::now();
        if (now - last_stats >= std::chrono::seconds(1)) {
            last_stats = now;
            if (app.running && app.src.lost()) {
                app.lost = true;
                fg.stop();
                app.running = false;
                app.src.close();
                srv.send_error("source", "source lost, retrying");
                app.rescan();
                app.publish(true);
            }
            web::JsonWriter w;
            w.begin_obj().key("overflows").num(static_cast<double>(app.src.overflows())).key("source_lost").boolean(app.lost).end();
            srv.set_stats_extra(w.out.substr(1, w.out.size() - 2));
        }
        if (!app.running && now - last_retry >= std::chrono::seconds(2)) {
            last_retry = now;
            if (app.kind != SourceMux::Kind::None) app.select(fg, app.kind, app.id, app.tuned(), app.rate, true);
            else if (auto_pick) { app.rescan(); pick_only_device(); }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (app.running) fg.stop();
    srv.stop();
    return 0;
}
