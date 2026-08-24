// Browser receiver: SourceMux -> fanout -> {spectrum, shift -> resampler -> demod} -> WebSink,
// with gated decoder taps (RDS off the 240 kHz channel, APRS off the 48 kHz audio, AIS off a
// 96 kHz resample) publishing JSON text frames.
//   ./earshot [--source sim|hackrf[:serial]|none] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]
//            [--gain NAME=V]... [--decoder none|rds|aprs|ais] [--port N] [--bind ADDR] [--token T]
//            [--client-dir DIR] [--record-dir DIR] [--record-max-bytes N] [--state-file FILE] [--version]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_mux.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/utils/gate.hpp"
#include "desktop_blocks/spectrum/spectrum.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/demod/analog_demod.hpp"
#include "desktop_blocks/sigmf/recorder_sigmf.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/fm/fm_mpx_decoder.hpp"
#include "desktop_blocks/aprs/afsk_demod.hpp"
#include "desktop_blocks/filters/kaiser_lpf.hpp"
#include "desktop_blocks/ais/ais_decoder.hpp"
#include "desktop_blocks/web/json_sink.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/web_sink.hpp"
#include "decoder_json.hpp"
#include "recordings.hpp"
#include "recordings_route.hpp"
#include "watchdog.hpp"
#include "earshot_client_files.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "earshot_version.hpp"
#ifndef EARSHOT_VERSION
#define EARSHOT_VERSION "dev"
#endif

static std::atomic<bool> g_run{true};

static constexpr double CHANNEL_HZ = 240e3;    // demod input rate and channel width
static constexpr double IF_OFFSET = 400e3;     // keeps the SDR's DC spike out of the channel
static constexpr double AUDIO_HZ = 48e3;       // AnalogDemodBlock output rate
static constexpr double AIS_HZ = 96e3;         // 10 samples per AIS symbol
static constexpr double APRS_DEVIATION_HZ = 5e3;
static constexpr double APRS_CHANNEL_HZ = 15e3;   // 2 m NBFM: +/-7.5 kHz, so a louder neighbour cannot capture the discriminator

// Every decoder hangs off a fixed-rate point in the chain, so none of them care
// which source or sample rate is selected — only where the receiver is tuned.
struct DecoderInfo {
    const char* id;
    double bands[2][2];   // {lo, hi} pairs, {0,0} unused
};
static const DecoderInfo DECODERS[] = {
    {"none", {{0, 0}, {0, 0}}},
    {"rds",  {{87.5e6, 108e6}, {0, 0}}},
    {"aprs", {{144.38e6, 144.40e6}, {144.79e6, 144.81e6}}},
    {"ais",  {{161.97e6, 161.99e6}, {162.01e6, 162.03e6}}},
};

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
    return SourceMux::parse_id(s, kind, id);
}
static std::string source_id(SourceMux::Kind kind, const std::string& id) {
    return SourceMux::format_id(kind, id);
}

// The decoder taps add a dozen mostly-idle blocks. PinnedIslands parks them
// instead of polling a thread each, and splits the rest by measured cost over a
// topological sort, so the live path is never stuck behind one worker.
// FixedThreadPool must not be used here: it chunks in declaration order, so one
// worker inherits source->fanout->spectrum->shift->resampler and the front end
// caps at a single core.
static cler::FlowGraphConfig graph_config() {
    cler::FlowGraphConfig c;
    c.scheduler = cler::SchedulerType::PinnedIslands;
    c.num_workers = 4;
    // so the choice above can be re-measured on the deployment box rather than argued about
    if (const char* s = std::getenv("EARSHOT_SCHEDULER")) {
        if (std::string(s) == "thread_per_block") c.scheduler = cler::SchedulerType::ThreadPerBlock;
        else if (std::string(s) != "pinned_islands") cler::panic("EARSHOT_SCHEDULER: pinned_islands or thread_per_block");
    }
    return c;
}

static constexpr uint64_t MIN_FREE_BYTES = 200ull << 20;
static constexpr uint64_t DEFAULT_RECORD_MAX_BYTES = 20ull << 30;
// the source retry runs every 2 s, so this is many attempts, not a hair trigger
static constexpr auto LOST_GRACE = std::chrono::seconds(60);

using earshot::free_disk;

struct App {
    SourceMux& src;
    FanoutBlock<std::complex<float>>& fan;
    SpectrumBlock& spec;
    FrequencyShiftBlock& shift;
    MultiStageResamplerBlock<std::complex<float>>& resamp;
    AnalogDemodBlock& demod;
    SigMFRecorderBlock& rec;
    web::WebServer& srv;
    WebSinkBlock& sink;
    GateBlock<std::complex<float>>& rds_gate;
    GateBlock<std::complex<float>>& ais_gate;
    GateBlock<std::complex<float>>& aprs_gate;
    FMMpxDecoderBlock& mpx;

    std::string decoder = "none";
    std::string record_dir, state_file;
    uint64_t record_max_bytes = DEFAULT_RECORD_MAX_BYTES;
    uint64_t pruned_bytes = 0;
    std::vector<std::pair<std::string, double>> gains;
    double rate, center, offset = 0.0;
    AnalogDemodBlock::Mode mode;
    uint32_t gen = 0;
    bool running = false, switching = false, lost = false;
    SourceMux::Kind kind = SourceMux::Kind::None;
    std::string id;
    std::vector<SourceMux::DeviceInfo> devices;
    std::vector<SourceMux::Control> caps;
    std::mutex health_mutex;
    std::string health_json = "{}";

    App(SourceMux& s, FanoutBlock<std::complex<float>>& f, SpectrumBlock& sp, FrequencyShiftBlock& sh,
        MultiStageResamplerBlock<std::complex<float>>& r, AnalogDemodBlock& d, SigMFRecorderBlock& rc,
        web::WebServer& server, WebSinkBlock& k, GateBlock<std::complex<float>>& rg,
        GateBlock<std::complex<float>>& ag, GateBlock<std::complex<float>>& pg, FMMpxDecoderBlock& mp,
        double rate_hz, double freq_hz, AnalogDemodBlock::Mode m)
        : src(s), fan(f), spec(sp), shift(sh), resamp(r), demod(d), rec(rc), srv(server), sink(k),
          rds_gate(rg), ais_gate(ag), aprs_gate(pg), mpx(mp),
          rate(rate_hz), center(freq_hz), mode(m) {}

    bool set_decoder(const std::string& name) {
        bool known = false;
        for (const auto& d : DECODERS) known = known || name == d.id;
        if (!known) return false;
        decoder = name;
        rds_gate.set_open(name == "rds");
        aprs_gate.set_open(name == "aprs");
        ais_gate.set_open(name == "ais");
        if (name == "rds") mpx.rds_reset();
        return true;
    }

    uint64_t decoder_dropped() const { return rds_gate.dropped() + aprs_gate.dropped() + ais_gate.dropped(); }

    // A dropped window leaves the RDS bit sync and station snapshot describing
    // signal that no longer arrived; AFSK and AIS hunt for their own preambles
    // and resync on their own.
    void reset_decoder_state() {
        if (decoder == "rds") mpx.rds_reset();
    }

    void reset_taps() {
        rds_gate.clear_dropped();
        aprs_gate.clear_dropped();
        ais_gate.clear_dropped();
        reset_decoder_state();
    }

    void publish_rds() {
        if (decoder != "rds") return;
        web::JsonWriter w;
        web::to_json(mpx.rds_station(), w);
        srv.push_text("rds", w.out);
    }

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
         .key("recording").boolean(rec.recording())
         .key("is_file").boolean(running && src.is_file())
         .key("paused").boolean(src.paused()).key("loop").boolean(src.looping())
         .key("decoder").str(decoder);
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
        w.end().key("spectrum").begin_obj().key("n").num(1024).key("fps").num(20).end();
        w.key("decoders").begin_arr();
        for (const auto& d : DECODERS) {
            w.begin_obj().key("id").str(d.id).key("available").boolean(true);
            if (d.bands[0][1] > 0.0) {
                w.key("expects").begin_arr();
                for (const auto& b : d.bands) {
                    if (b[1] > 0.0) w.begin_obj().key("min").num(b[0]).key("max").num(b[1]).end();
                }
                w.end();
            }
            w.end();
        }
        w.begin_obj().key("id").str("adsb").key("available").boolean(false)
         .key("reason").str("needs a full-rate 1090 MHz magnitude tap and CPR aggregation").end();
        w.end().end();
        return w.out;
    }

    // state to every tab; hello too when the device list or controls changed
    void publish(bool hello = false) {
        caps = src.capabilities();
        srv.set_state(state_json());
        if (hello) { srv.set_hello_extra(hello_json()); srv.resend_hello(); }
        web::JsonWriter h;
        h.begin_obj().key("version").str(EARSHOT_VERSION).key("uptime_s").num(srv.uptime_seconds())
         .key("clients").num(srv.client_count())
         .key("source").str(source()).key("rate").num(rate)
         .key("overflows").num(src.overflows()).key("recording").boolean(rec.recording())
         .key("free_disk").num(free_disk(record_dir)).end();
        {
            std::lock_guard<std::mutex> lock(health_mutex);
            health_json = std::move(h.out);
        }
        if (running) save_state();
    }

    // the HTTP thread only ever reads this snapshot; App's own fields are the
    // main thread's
    std::string health() {
        std::lock_guard<std::mutex> lock(health_mutex);
        return health_json;
    }

    template <typename FG>
    bool select(FG& fg, SourceMux::Kind k, const std::string& dev, double freq_hz, double rate_hz, bool quiet = false) {
        if (rec.recording()) {
            if (!quiet) { srv.send_error("source", "stop recording before switching"); return false; }
            rec.stop();
            srv.send_error("record", "recording stopped: source lost");
        }
        const bool same = running && k == kind && (dev == id || dev.empty());
        std::string why;
        if (!same && !src.probe(k, dev, &why)) {
            if (!quiet) srv.send_error("source", why, source_id(k, dev));
            return false;
        }
        const SourceMux::Kind prev_kind = kind;
        const std::string prev_id = id;
        switching = true;
        srv.set_state(state_json());
        if (running) { fg.stop(); running = false; }
        const bool ok = src.select(k, dev, freq_hz, rate_hz, &why);
        switching = false;
        kind = k; id = dev;
        rescan();
        if (!ok) {
            kind = prev_kind; id = prev_id;
            if (!quiet) { srv.send_error("source", why, source_id(k, dev)); publish(true); }
            return false;
        }
        rate = src.rate(); center = src.center(); offset = 0.0; lost = false;
        if (std::fabs(center - freq_hz) > rate) freq_hz = center + IF_OFFSET;
        for (const auto& g : gains) src.set(g.first, g.second);
        resamp.set_ratio(static_cast<float>(CHANNEL_HZ / rate));
        rec.set_rate(rate);
        spec.set_rate(rate);
        shift.set_sample_rate(rate);
        tune_to(freq_hz);
        fg.reset();
        reset_taps();
        fg.run(graph_config());
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
        if (rec.recording()) {
            const double off = hz - center;
            if (fits(off)) tune_offset(off);
            else srv.send_error("set", "recording: only offset tuning inside the band");
            return;
        }
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

    static std::string sanitize(const std::string& in) {
        std::string out;
        for (char c : in) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out += c;
        return out.substr(0, 64);
    }

    // Keeps the record dir inside its byte cap and off the free-space floor by
    // deleting whole recordings, oldest first; the one being written is spared.
    void prune() {
        const auto p = earshot::prune_recordings(record_dir, record_max_bytes, MIN_FREE_BYTES, rec.base());
        if (p.recordings == 0) return;
        pruned_bytes += p.bytes;
        srv.send_error("record", "pruned " + std::to_string(p.recordings) + " old recording(s), " +
                                 std::to_string(p.bytes >> 20) + " MB");
    }

    void record(bool on, const std::string& name) {
        if (!on) { rec.stop(); publish(); return; }
        if (record_dir.empty()) { srv.send_error("record", "no --record-dir"); return; }
        if (!running || switching) { srv.send_error("record", "no running source"); return; }
        if (rec.recording()) return;
        // floor first: on a full disk, deleting the archive and then failing
        // anyway would cost the client their captures for nothing
        if (free_disk(record_dir) < MIN_FREE_BYTES) { srv.send_error("record", "less than 200 MB free"); return; }
        prune();
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%S", std::gmtime(&now));
        const std::string prefix = sanitize(name);
        std::string base = record_dir + "/" + (prefix.empty() ? "" : prefix + "_") + stamp + "_" +
                           std::to_string(static_cast<long long>(center));
        if (!rec.start_at(base, center)) { srv.send_error("record", "cannot open " + base); return; }
        publish();
    }

    void save_state() {
        if (state_file.empty()) return;
        web::JsonWriter w;
        w.begin_obj().key("source").str(source_id(kind, id)).key("freq").num(tuned()).key("rate").num(rate)
         .key("mode").str(AnalogDemodBlock::mode_name(mode)).key("decoder").str(decoder);
        for (const auto& c : caps) if (!c.ro && c.id != "freq") w.key(c.id).num(c.value);
        w.end();
        std::ofstream(state_file) << w.out;
    }
};

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    web::ServerOptions o;
    o.files = EARSHOT_CLIENT_FILES;
    o.file_count = EARSHOT_CLIENT_FILES_COUNT;
    o.version = EARSHOT_VERSION;
    o.audio_rate = AUDIO_HZ;
    std::string source, record_dir, state_file, mode_s, decoder;
    double freq = 0, rate = 0;
    uint64_t record_max_bytes = DEFAULT_RECORD_MAX_BYTES;
    std::vector<std::pair<std::string, double>> gains;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> const char* { return (!std::strcmp(argv[i], flag) && i + 1 < argc) ? argv[++i] : nullptr; };
        if (!std::strcmp(argv[i], "--version")) { std::printf("earshot %s\n", EARSHOT_VERSION); return 0; }
        else if (const char* v = next("--source")) source = v;
        else if (const char* v = next("--freq")) freq = std::atof(v);
        else if (const char* v = next("--rate")) rate = std::atof(v);
        else if (const char* v = next("--mode")) mode_s = v;
        else if (const char* v = next("--gain")) {
            const char* eq = std::strchr(v, '=');
            if (!eq) { std::fprintf(stderr, "--gain NAME=V\n"); return 1; }
            gains.emplace_back(std::string(v, eq), std::atof(eq + 1));
        }
        else if (const char* v = next("--decoder")) decoder = v;
        else if (const char* v = next("--port")) o.port = std::atoi(v);
        else if (const char* v = next("--bind")) o.bind = v;
        else if (const char* v = next("--token")) o.token = v;
        else if (const char* v = next("--client-dir")) o.client_dir = v;
        else if (const char* v = next("--record-dir")) record_dir = v;
        else if (const char* v = next("--record-max-bytes")) {
            if (!earshot::parse_bytes(v, record_max_bytes)) {
                std::fprintf(stderr, "--record-max-bytes wants a byte count (20e9, 5000000000, or 0 for unlimited), got '%s'\n", v);
                return 1;
            }
        }
        else if (const char* v = next("--state-file")) state_file = v;
        else {
            std::fprintf(stderr, "Usage: %s [--source sim|hackrf[:serial]|none] [--freq Hz] [--rate Hz] [--mode WBFM|NBFM|AM|USB|LSB]\n"
                                 "          [--gain NAME=V]... [--decoder none|rds|aprs|ais] [--port N] [--bind ADDR] [--token T]\n"
                                 "          [--client-dir DIR] [--record-dir DIR] [--record-max-bytes N] [--state-file FILE] [--version]\n", argv[0]);
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
            if (decoder.empty()) decoder = web::json_str(fields, "decoder");
            for (const auto& [k, v] : fields) {
                if (k == "source" || k == "freq" || k == "rate" || k == "mode" || k == "decoder") continue;
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
        if (!ur || std::fread(rnd, 1, sizeof rnd, ur) != sizeof rnd) cler::panic("earshot: /dev/urandom");
        std::fclose(ur);
        char hex[sizeof rnd * 2 + 1];
        for (size_t i = 0; i < sizeof rnd; ++i) std::snprintf(hex + 2 * i, 3, "%02x", rnd[i]);
        o.token = hex;
    }
    web::WebServer srv(o);
    SourceMux src("Source");
    FanoutBlock<std::complex<float>> fan("RF fanout", 3, 1 << 20);
    SpectrumBlock spec("Spectrum", rate, 1024, 20.0f, -120.0f, 0.5f, 4, SpectralWindow::Hann, 1 << 16);
    FrequencyShiftBlock shift("Tune shift", 0.0, rate, 1 << 18);
    MultiStageResamplerBlock<std::complex<float>> resamp("Channel", static_cast<float>(CHANNEL_HZ / rate), 60.0f, 1 << 18);
    AnalogDemodBlock demod("Demod", CHANNEL_HZ, mode, 1 << 16);
    SigMFRecorderBlock rec("Recorder", rate, 1 << 20);
    WebSinkBlock sink("Web sink", srv);
    FanoutBlock<std::complex<float>> chanfan("Channel fanout", 4, 1 << 18);
    GateBlock<std::complex<float>> rds_gate("RDS gate", false, 1 << 18);
    FMDemodBlock fmdemod("FM demod", CHANNEL_HZ, 75e3, 1 << 16);
    FMMpxDecoderBlock mpx("MPX decoder", CHANNEL_HZ, 5, 50.0, 1 << 16);
    SinkNullBlock<float> mpx_drain("MPX audio drain");
    GateBlock<std::complex<float>> ais_gate("AIS gate", false, 1 << 18);
    MultiStageResamplerBlock<std::complex<float>> ais_resamp("AIS channel", static_cast<float>(AIS_HZ / CHANNEL_HZ), 60.0f, 1 << 16);
    AISDecoderBlock ais("AIS", AIS_HZ, 1 << 16);
    web::JsonTextSinkBlock<ais::Message> ais_json("AIS json", srv, "ais");
    GateBlock<std::complex<float>> aprs_gate("APRS gate", false, 1 << 18);
    MultiStageResamplerBlock<std::complex<float>> aprs_resamp("APRS channel", static_cast<float>(AUDIO_HZ / CHANNEL_HZ), 60.0f, 1 << 16);
    KaiserLPFBlock<std::complex<float>> aprs_lpf("APRS channel filter", AUDIO_HZ, APRS_CHANNEL_HZ / 2, 3e3, 60.0, 1 << 16);
    FMDemodBlock aprs_fm("APRS NBFM", AUDIO_HZ, APRS_DEVIATION_HZ, 1 << 16);
    AFSKDemodBlock afsk("AFSK1200", AUDIO_HZ, 1 << 14);
    web::JsonTextSinkBlock<aprs::Packet> aprs_json("APRS json", srv, "aprs");
    if (!record_dir.empty()) src.set_sigmf_dir(record_dir);
    App app(src, fan, spec, shift, resamp, demod, rec, srv, sink, rds_gate, ais_gate, aprs_gate, mpx,
            rate, freq, mode);
    app.gains = gains;
    app.record_dir = record_dir;
    app.record_max_bytes = record_max_bytes;
    app.state_file = state_file;
    if (!decoder.empty() && !app.set_decoder(decoder)) {
        std::fprintf(stderr, "unknown --decoder %s\n", decoder.c_str());
        return 1;
    }

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &fan.in),
        cler::BlockRunner(&fan, &spec.in, &shift.in, &rec.in),
        cler::BlockRunner(&rec),
        cler::BlockRunner(&spec, &sink.spectrum),
        cler::BlockRunner(&shift, &resamp.in),
        cler::BlockRunner(&resamp, &chanfan.in),
        cler::BlockRunner(&chanfan, &demod.in, &rds_gate.in, &ais_gate.in, &aprs_gate.in),
        cler::BlockRunner(&rds_gate, &fmdemod.in),
        cler::BlockRunner(&fmdemod, &mpx.in),
        cler::BlockRunner(&mpx, &mpx_drain.in),
        cler::BlockRunner(&mpx_drain),
        cler::BlockRunner(&ais_gate, &ais_resamp.in),
        cler::BlockRunner(&ais_resamp, &ais.in),
        cler::BlockRunner(&ais, &ais_json.in),
        cler::BlockRunner(&ais_json),
        cler::BlockRunner(&aprs_gate, &aprs_resamp.in),
        cler::BlockRunner(&aprs_resamp, &aprs_lpf.in),
        cler::BlockRunner(&aprs_lpf, &aprs_fm.in),
        cler::BlockRunner(&aprs_fm, &afsk.in),
        cler::BlockRunner(&afsk, &aprs_json.in),
        cler::BlockRunner(&aprs_json),
        cler::BlockRunner(&demod, &sink.audio),
        cler::BlockRunner(&sink));

    srv.add_http_route("/health", [&app](const std::string&, const std::string&) {
        return web::HttpReply{200, app.health(), "application/json"};
    });
    if (!record_dir.empty()) {
        srv.add_http_route("/recordings", [record_dir](const std::string& path, const std::string&) {
            return earshot::recordings_route(record_dir, path);
        });
    }
    app.rescan();
    app.publish(true);
    srv.start();
    if (o.token.empty()) std::printf("earshot %s on http://%s:%d/\n", EARSHOT_VERSION, o.bind.c_str(), o.port);
    else std::printf("earshot %s on http://%s:%d/?token=%s\n", EARSHOT_VERSION, o.bind.c_str(), o.port, o.token.c_str());

    const bool auto_pick = source.empty();
    auto pick_only_device = [&]() {
        std::vector<SourceMux::DeviceInfo> real;
        for (const auto& d : app.devices)
            if (d.kind != SourceMux::Kind::Sim && d.kind != SourceMux::Kind::SigMF) real.push_back(d);
        if (real.size() == 1) app.select(fg, real[0].kind, real[0].id, freq, rate);
    };
    if (k != SourceMux::Kind::None) app.select(fg, k, id, freq, rate);
    else if (auto_pick) pick_only_device();

    using clock = std::chrono::steady_clock;
    auto last_stats = clock::now(), last_retry = clock::now(), last_ping = clock::now();
    uint64_t last_decoder_dropped = 0;
    earshot::SdNotify notify;
    notify.send("READY=1");
    uint64_t last_delivered = srv.sent();
    auto lost_since = clock::now();
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
                    else if (key == "decoder") {
                        if (!app.set_decoder(web::json_str(f, "decoder")))
                            srv.send_error("set", "unknown or unavailable decoder " + web::json_str(f, "decoder"));
                    }
                    else if (!app.set_control(key, v)) srv.send_error("set", "unknown or read-only control " + key);
                }
                app.publish();
            } else if (t == "source") {
                if (parse_source(web::json_str(f, "id"), k, id)) {
                    const double r = web::json_num(f, "rate");
                    app.select(fg, k, id, app.tuned(), r > 0 ? r : app.rate);
                } else srv.send_error("source", "unknown source", web::json_str(f, "id"));
            } else if (t == "record") {
                app.record(web::json_str(f, "on") == "true", web::json_str(f, "name"));
            } else if (t == "play") {
                const std::string name = web::json_str(f, "name");
                bool handled = false;
                if (!name.empty()) { app.select(fg, SourceMux::Kind::SigMF, name, app.tuned(), app.rate); handled = true; }
                if (app.src.is_file()) {
                    for (const auto& [key, val] : f) {
                        if (key == "pos") { app.src.seek(std::atof(val.c_str())); handled = true; }
                        else if (key == "pause") { app.src.pause(val == "true"); handled = true; }
                        else if (key == "loop") { app.src.set_loop(val == "true"); handled = true; }
                    }
                    app.publish();
                } else if (!handled) srv.send_error("play", "no file source");
            } else if (t == "rescan") {
                app.rescan();
                app.publish(true);
            }
        }
        const auto now = clock::now();
        if (now - last_stats >= std::chrono::seconds(1)) {
            last_stats = now;
            app.publish_rds();
            if (const uint64_t dropped = app.decoder_dropped(); dropped != last_decoder_dropped) {
                last_decoder_dropped = dropped;
                app.reset_decoder_state();
            }
            if (app.running && app.src.lost()) {
                app.lost = true;
                fg.stop();
                app.running = false;
                app.src.close();
                srv.send_error("source", "source lost, retrying");
                app.rescan();
                app.publish(true);
            }
            if (app.rec.take_failure()) {
                srv.send_error("record", "write failed, recording stopped");
                app.publish();
            }
            if (app.rec.recording()) {
                app.prune();
                if (free_disk(record_dir) < MIN_FREE_BYTES) {
                    app.rec.stop();
                    srv.send_error("record", "disk almost full, recording stopped");
                    app.publish();
                }
            }
            web::JsonWriter w;
            w.begin_obj().key("overflows").num(app.src.overflows()).key("source_lost").boolean(app.lost)
             .key("rec_bytes").num(app.rec.bytes()).key("free_bytes").num(free_disk(record_dir))
             .key("pruned_bytes").num(app.pruned_bytes)
             .key("decoder_dropped").num(app.decoder_dropped());
            if (app.running && app.src.is_file()) {
                w.key("pos").num(app.src.pos_seconds()).key("duration").num(app.src.duration_seconds())
                 .key("ended").boolean(app.src.ended());
            }
            w.end();
            srv.set_stats_extra(w.out);
        }
        if (!app.lost) lost_since = now;
        // Only ping while the chain is actually delivering, so systemd restarts a
        // process that is alive but wedged. One skipped ping is harmless: the
        // deadline is twice the interval.
        if (notify.interval().count() > 0 && now - last_ping >= notify.interval()) {
            last_ping = now;
            const uint64_t delivered = srv.sent();
            earshot::Health h;
            h.running = app.running;
            h.lost = app.lost;
            h.paused = app.src.paused();
            h.ended = app.src.ended();
            h.delivered = delivered != last_delivered;
            h.lost_for = now - lost_since;
            last_delivered = delivered;
            if (earshot::flowing(h, LOST_GRACE)) notify.send("WATCHDOG=1");
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
