#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "websdr_client_files.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>

static std::atomic<bool> g_run{true};

static std::string state_json(uint32_t gen, double freq, const std::string& mode, double gain, const std::string& source) {
    web::JsonWriter w;
    w.begin_obj().key("gen").num(gen).key("source").str(source).key("freq").num(freq).key("rate").num(2.4e6)
     .key("mode").str(mode).key("gain").num(gain).key("offset").num(0).key("passband").num(200e3).key("recording").boolean(false).end();
    return w.out;
}

int main(int argc, char** argv) {
    web::ServerOptions o;
    o.files = WEBSDR_CLIENT_FILES;
    o.file_count = WEBSDR_CLIENT_FILES_COUNT;
    o.version = "web_demo";
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> const char* { return (!std::strcmp(argv[i], flag) && i + 1 < argc) ? argv[++i] : nullptr; };
        if (const char* v = next("--port")) o.port = std::atoi(v);
        else if (const char* v = next("--bind")) o.bind = v;
        else if (const char* v = next("--token")) o.token = v;
        else if (const char* v = next("--client-dir")) o.client_dir = v;
    }
    std::signal(SIGINT, [](int) { g_run = false; });
    std::signal(SIGTERM, [](int) { g_run = false; });

    web::WebServer srv(o);
    uint32_t gen = 1;
    double freq = 100e6, gain = 20;
    std::string mode = "WBFM", source;
    srv.set_hello_extra(
        "{\"sources\":[{\"id\":\"sim\",\"kind\":\"sim\",\"label\":\"Simulator\",\"available\":true},"
        "{\"id\":\"hackrf:0\",\"kind\":\"hackrf\",\"label\":\"HackRF One (demo, unavailable)\",\"available\":false}],"
        "\"controls\":[{\"id\":\"freq\",\"label\":\"frequency\",\"type\":\"range\",\"min\":1e6,\"max\":6e9,\"step\":1e3,\"unit\":\"Hz\"},"
        "{\"id\":\"mode\",\"label\":\"mode\",\"type\":\"enum\",\"options\":[\"WBFM\",\"NBFM\",\"AM\",\"USB\",\"LSB\"]},"
        "{\"id\":\"gain\",\"label\":\"gain\",\"type\":\"range\",\"min\":0,\"max\":40,\"step\":1,\"unit\":\"dB\"},"
        "{\"id\":\"amp\",\"label\":\"amp\",\"type\":\"bool\"},"
        "{\"id\":\"rate\",\"label\":\"sample rate\",\"type\":\"range\",\"min\":2.4e6,\"max\":2.4e6,\"step\":1,\"unit\":\"Hz\",\"ro\":true}],"
        "\"spectrum\":{\"n\":1024,\"fps\":20}}");
    srv.set_gen(gen);
    srv.set_state(state_json(gen, freq, mode, gain, source));
    srv.add_http_route("/health", [&srv, &o](const std::string&, const std::string&) {
        web::JsonWriter w;
        w.begin_obj().key("version").str(o.version).key("uptime_s").num(srv.uptime_seconds())
         .key("clients").num(srv.client_count()).key("source").str("sim").end();
        return web::HttpReply{200, w.out, "application/json"};
    });
    srv.start();
    std::printf("web_demo on http://%s:%d/\n", o.bind.c_str(), o.port);

    SpectrumFrame f{};
    f.n = 1024; f.rate_hz = 2.4e6; f.db_min = -120; f.db_step = 0.5f;
    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 3.0f);
    std::vector<int16_t> pcm(web::AUDIO_CHUNK);
    double phase = 0.0;
    uint64_t tick = 0;
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (g_run) {
        std::string ctl;
        while (srv.pop_control(ctl)) {
            web::Fields fields;
            if (!web::json_parse_object(ctl, fields)) continue;
            const std::string t = web::json_str(fields, "t");
            bool changed = false;
            if (t == "set") {
                if (web::json_find(fields, "freq")) { freq = web::json_num(fields, "freq"); changed = true; }
                if (web::json_find(fields, "gain")) { gain = web::json_num(fields, "gain"); changed = true; }
                if (web::json_find(fields, "mode")) { mode = web::json_str(fields, "mode"); changed = true; }
            } else if (t == "source") {
                source = web::json_str(fields, "id"); changed = true;
            } else if (t == "rescan") {
                srv.set_state(state_json(gen, freq, mode, gain, source));
            }
            if (changed) { ++gen; srv.set_gen(gen); srv.set_state(state_json(gen, freq, mode, gain, source)); }
        }
        if (tick % 2 == 0) {
            f.gen = gen; f.center_hz = freq;
            const double tone_bin = 512 + 200 * std::sin(tick / 200.0);
            for (int i = 0; i < f.n; ++i) {
                float db = -100.0f + noise(rng) + static_cast<float>(gain) * 0.5f;
                const double d = i - tone_bin;
                db += static_cast<float>(40.0 * std::exp(-d * d / 18.0));
                const float q = (db - f.db_min) / f.db_step;
                f.bins[i] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, q)));
            }
            srv.push_spectrum(f);
        }
        for (size_t i = 0; i < pcm.size(); ++i) {
            phase += 2 * M_PI * (mode == "AM" ? 880.0 : 440.0) / 48000.0;
            pcm[i] = static_cast<int16_t>(0.2 * 32767 * std::sin(phase));
        }
        srv.push_audio(pcm.data(), pcm.size());
        ++tick;
        next += std::chrono::milliseconds(20);
        std::this_thread::sleep_until(next);
    }
    srv.stop();
    return 0;
}
