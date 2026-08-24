// owrx_connector-compatible IQ source: SourceMux -> loopback TCP, so OpenWebRX
// can use any radio cler supports.
//   cler_connector -p 4950 -c 4951 -s 2400000 -f 100000000 -d hackrf [-g LNA=24,VGA=20] [-i]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_mux.hpp"
#include "desktop_examples/cler_connector/connector_net.hpp"
#include "desktop_examples/cler_connector/connector_proto.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// OWRX's feature check greps "^<configured binary name> version (.*)" and wants
// >= 0.5, so the name comes from argv[0] and the number never goes below that.
static constexpr const char* CONNECTOR_VERSION = "0.6";

static std::atomic<bool> g_run{true};

static bool parse_device(const std::string& s, SourceMux::Kind& kind, std::string& id) {
    const size_t colon = s.find(':');
    const std::string head = s.substr(0, colon);
    id = colon == std::string::npos ? "" : s.substr(colon + 1);
    for (auto k : {SourceMux::Kind::HackRF, SourceMux::Kind::Pluto, SourceMux::Kind::UHD,
                   SourceMux::Kind::Cariboulite, SourceMux::Kind::Soapy, SourceMux::Kind::SigMF,
                   SourceMux::Kind::Sim}) {
        if (head == SourceMux::kind_name(k)) { kind = k; return true; }
    }
    // A soapy-style "driver=hackrf,serial=.." reaches us verbatim from OWRX's device field.
    if (s.find('=') != std::string::npos) { kind = SourceMux::Kind::Soapy; id = s; return true; }
    return false;
}

static std::string device_name(SourceMux::Kind kind, const std::string& id) {
    std::string s = SourceMux::kind_name(kind);
    if (!id.empty()) s += ":" + id;
    return s;
}

struct App {
    SourceMux& src;
    conn::IqSinkBlock& sink;
    SourceMux::Kind kind = SourceMux::Kind::None;
    std::string id;
    double freq = 100e6;
    double rate = 2.4e6;
    double ppm = 0;
    conn::GainSpec gain;
    std::string antenna;
    bool running = false;
    bool complained = false;

    // The oscillator error is corrected by asking the hardware for a frequency
    // off by the same fraction; OWRX applies no correction of its own.
    double corrected() const { return freq * (1.0 + ppm * 1e-6); }

    void tune() {
        src.set("freq", corrected());
        if (ppm == 0) freq = src.center();
    }

    void apply_gain() {
        const auto caps = src.capabilities();
        auto has = [&](const std::string& want) {
            for (const auto& c : caps) if (c.id == want && !c.ro) return true;
            return false;
        };
        if (gain.automatic) {
            if (has("agc")) src.set("agc", 1);
            return;
        }
        if (has("agc")) src.set("agc", 0);
        if (gain.single) {
            if (has("gain")) { src.set("gain", gain.value); return; }
            // HackRF splits its gain across two stages; OWRX only ever sends one number.
            if (has("lna") && has("vga")) {
                const double lna = std::min(40.0, std::floor(gain.value / 8.0) * 8.0);
                src.set("lna", std::max(0.0, lna));
                src.set("vga", std::clamp(gain.value - lna, 0.0, 62.0));
            }
            return;
        }
        for (const auto& [name, v] : gain.pairs) {
            const std::string want = conn::lower(name);
            bool applied = false;
            for (const auto& c : caps) {
                if (c.ro) continue;
                if (conn::lower(c.id) == want || conn::lower(c.id) == "gain_" + want) {
                    src.set(c.id, v);
                    applied = true;
                }
            }
            if (!applied) std::fprintf(stderr, "cler_connector: no gain named '%s'\n", name.c_str());
        }
    }

    void apply_antenna() {
        if (antenna.empty()) return;
        for (const auto& c : src.capabilities()) {
            if (c.id != "antenna") continue;
            for (size_t i = 0; i < c.options.size(); ++i) {
                if (c.options[i] == antenna) { src.set("antenna", static_cast<double>(i)); return; }
            }
            std::fprintf(stderr, "cler_connector: no antenna named '%s'\n", antenna.c_str());
        }
    }

    template <typename FG>
    bool open(FG& fg) {
        if (running) { fg.stop(); running = false; }
        // The device has to be released before probing it, or we would be
        // asking whether the handle we are still holding can be opened again.
        src.close();
        if (kind == SourceMux::Kind::None || !src.probe(kind, id)) {
            if (!complained) {
                std::fprintf(stderr, "cler_connector: cannot open %s, retrying\n", device_name(kind, id).c_str());
                complained = true;
            }
            return false;
        }
        if (!src.select(kind, id, corrected(), rate)) {
            if (!complained) {
                std::fprintf(stderr, "cler_connector: %s refused the requested settings\n", device_name(kind, id).c_str());
                complained = true;
            }
            return false;
        }
        apply_gain();
        apply_antenna();
        rate = src.rate();
        if (ppm == 0) freq = src.center();
        fg.reset();
        fg.run();
        running = true;
        complained = false;
        std::fprintf(stderr, "cler_connector: %s at %.6f MHz, %.6f MS/s\n",
                     device_name(kind, id).c_str(), freq / 1e6, rate / 1e6);
        return true;
    }
};

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const conn::Options o = conn::parse_args(argc, argv);
    const std::string self = conn::basename_of(argv[0]);
    if (o.version) {
        std::printf("%s version %s\n", self.c_str(), CONNECTOR_VERSION);
        return 0;
    }
    if (o.help || !o.error.empty()) {
        std::FILE* out = o.error.empty() ? stdout : stderr;
        if (!o.error.empty()) std::fprintf(stderr, "%s: %s\n", self.c_str(), o.error.c_str());
        std::fprintf(out,
                     "Usage: %s [-p iq_port] [-c control_port] [-s samplerate] [-f frequency]\n"
                     "          [-d device] [-g gain] [-a antenna] [-P ppm] [-i] [-v] [-h]\n"
                     "  -d  cler source: sim | hackrf[:serial] | pluto:ip:ADDR | uhd:ARGS |\n"
                     "      cariboulite:s1g|hif | soapy:ARGS | sigmf:NAME, or a bare soapy arg string.\n"
                     "      Empty picks the first device found.\n"
                     "  -g  auto | none | <dB> | NAME=V,NAME=V\n"
                     "  -r  rtl_tcp compatibility is not implemented.\n", self.c_str());
        return o.error.empty() ? 0 : 1;
    }
    // Accepting -r silently would leave OWRX pointing a TcpSource at a port
    // nothing listens on, which looks like a broken radio rather than a
    // missing feature.
    if (o.rtltcp_port >= 0) {
        std::fprintf(stderr, "%s: rtltcp compatibility is not implemented; turn rtltcp_compat off\n", self.c_str());
        return 1;
    }
    if (!o.settings.empty()) std::fprintf(stderr, "cler_connector: ignoring device settings '%s'\n", o.settings.c_str());

    std::signal(SIGINT, [](int) { g_run = false; });
    std::signal(SIGTERM, [](int) { g_run = false; });
    std::signal(SIGQUIT, [](int) { g_run = false; });
    std::signal(SIGPIPE, SIG_IGN);

    conn::IqServer iq(o.iq_port);
    conn::ControlServer ctl(o.control_port);
    SourceMux src("Source");
    conn::IqSinkBlock sink("IQ sink", iq);
    sink.iqswap.store(o.iqswap);
    std::fprintf(stderr, "cler_connector: IQ on 127.0.0.1:%d, control on %s\n",
                 iq.port(), o.control_port < 0 ? "(none)" : std::to_string(ctl.port()).c_str());

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &sink.in),
        cler::BlockRunner(&sink));

    App app{src, sink, SourceMux::Kind::None, "", 100e6, 2.4e6, o.ppm, {}, "", false, false};
    if (o.frequency > 0) app.freq = o.frequency;
    if (o.samp_rate > 0) app.rate = o.samp_rate;
    app.gain = conn::parse_gain(o.gain);
    app.antenna = o.antenna;
    if (!o.device.empty()) {
        if (!parse_device(o.device, app.kind, app.id)) {
            std::fprintf(stderr, "cler_connector: unknown device '%s'\n", o.device.c_str());
            return 1;
        }
    } else {
        for (const auto& d : src.enumerate()) {
            if (d.kind == SourceMux::Kind::Sim || d.kind == SourceMux::Kind::SigMF) continue;
            app.kind = d.kind;
            app.id = d.id;
            break;
        }
        if (app.kind == SourceMux::Kind::None) {
            std::fprintf(stderr, "cler_connector: no device found, falling back to the simulator\n");
            app.kind = SourceMux::Kind::Sim;
        }
    }
    app.open(fg);

    using clock = std::chrono::steady_clock;
    auto next_retry = clock::now();
    auto next_report = clock::now() + std::chrono::seconds(10);
    uint64_t reported_drops = 0;
    while (g_run) {
        std::string key, value;
        while (ctl.pop(key, value)) {
            const bool none = value == "None";
            double v = 0;
            const bool numeric = conn::parse_number(value, v);
            // OWRX sends the literal "None" when a property is cleared, which
            // means "back to the default", not "bad value".
            if (key == "center_freq") {
                if (numeric) { app.freq = v; app.tune(); }
                else if (!none) std::fprintf(stderr, "cler_connector: bad center_freq '%s'\n", value.c_str());
            } else if (key == "samp_rate") {
                // Every rate is fixed at construction downstream, so the graph
                // restarts; the sockets outlive it because OWRX keeps reading.
                if (numeric) { app.rate = v; app.open(fg); }
                else if (!none) std::fprintf(stderr, "cler_connector: bad samp_rate '%s'\n", value.c_str());
            } else if (key == "rf_gain") {
                app.gain = conn::parse_gain(none ? "auto" : value);
                if (app.running) app.apply_gain();
            } else if (key == "antenna") {
                app.antenna = none ? "" : value;
                if (app.running) app.apply_antenna();
            } else if (key == "iqswap") {
                sink.iqswap.store(!none && conn::truthy(value), std::memory_order_relaxed);
            } else if (key == "ppm") {
                app.ppm = none ? 0.0 : (numeric ? v : app.ppm);
                if (app.running) app.tune();
            } else if (key == "settings") {
                std::fprintf(stderr, "cler_connector: ignoring device settings '%s'\n", value.c_str());
            } else {
                std::fprintf(stderr, "cler_connector: ignoring control '%s'\n", key.c_str());
            }
        }
        const auto now = clock::now();
        if (app.running && src.lost()) {
            std::fprintf(stderr, "cler_connector: device lost\n");
            fg.stop();
            app.running = false;
            src.close();
            app.complained = false;
            next_retry = now + std::chrono::seconds(5);
        }
        if (!app.running && now >= next_retry) {
            next_retry = now + std::chrono::seconds(5);
            app.open(fg);
        }
        if (now >= next_report) {
            next_report = now + std::chrono::seconds(10);
            const uint64_t drops = iq.dropped();
            if (drops != reported_drops) {
                std::fprintf(stderr, "cler_connector: %llu samples dropped to slow readers\n",
                             static_cast<unsigned long long>(drops));
                reported_drops = drops;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (app.running) fg.stop();
    return 0;
}
