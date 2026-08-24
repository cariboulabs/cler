#include <gtest/gtest.h>
#include <ixwebsocket/IXHttpClient.h>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "tests/web_test_client.hpp"
#include "desktop_blocks/aprs/afsk_demod.hpp"
#include "desktop_blocks/filters/kaiser_lpf.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/sigmf/recorder_sigmf.hpp"
#include "desktop_blocks/web/json_sink.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_examples/websdr/decoder_json.hpp"
#include "desktop_examples/websdr/recordings_route.hpp"

using namespace web;
using webtest::free_port;
using webtest::TestClient;

TEST(WebServerTest, RecordingsListDownloadTraversalAndToken) {
    const std::string dir = testing::TempDir() + "/webrec";
    std::filesystem::create_directories(dir);
    {
        SigMFRecorderBlock rec("rec", 1e6, 1 << 16);
        ASSERT_TRUE(rec.start_at(dir + "/take1", 100e6));
        std::vector<std::complex<float>> tone(500, {0.25f, 0.0f});
        rec.in.writeN(tone.data(), tone.size());
        ASSERT_TRUE(rec.procedure().is_ok());
        rec.stop();
    }
    {
        std::ofstream bad(dir + "/bad.sigmf-meta");
        bad << "{\n  \"global\": { \"core:datatype\": \"cf64_le\", \"core:sample_rate\": 1e6 }\n}\n";
    }
    const int port = free_port();
    ServerOptions o; o.port = port; o.token = "s3";
    WebServer srv(o);
    srv.add_http_route("/recordings", [&dir](const std::string& path, const std::string&) {
        return websdr::recordings_route(dir, path);
    });
    srv.start();
    const std::string root = "http://127.0.0.1:" + std::to_string(port);
    ix::HttpClient http;

    EXPECT_EQ(http.get(root + "/recordings", http.createRequest())->statusCode, 401);
    auto list = http.get(root + "/recordings?token=s3", http.createRequest());
    ASSERT_EQ(list->statusCode, 200);
    EXPECT_NE(list->body.find("\"name\":\"take1\""), std::string::npos);
    EXPECT_EQ(list->body.find("bad"), std::string::npos);
    EXPECT_NE(list->body.find("\"bytes\":2000"), std::string::npos);
    EXPECT_NE(list->body.find("\"rate\":1000000"), std::string::npos);

    auto data = http.get(root + "/recordings/take1.sigmf-data?token=s3", http.createRequest());
    ASSERT_EQ(data->statusCode, 200);
    EXPECT_EQ(data->body.size(), 2000u);
    EXPECT_EQ(http.get(root + "/recordings/take1.sigmf-meta?token=s3", http.createRequest())->statusCode, 200);

    EXPECT_EQ(http.get(root + "/recordings/../take1.sigmf-data?token=s3", http.createRequest())->statusCode, 404);
    EXPECT_EQ(http.get(root + "/recordings/take1?token=s3", http.createRequest())->statusCode, 404);
    EXPECT_EQ(http.get(root + "/recordings/.hidden.sigmf-data?token=s3", http.createRequest())->statusCode, 404);
    srv.stop();
    std::filesystem::remove_all(dir);
}

TEST(JsonAdapters, AisMessageToJson) {
    ais::Message m{};
    m.type = 1; m.mmsi = 244660000; m.has_position = true; m.lat = 52.375; m.lon = 4.9;
    m.sog = 12.5f; m.cog = 271.3f; m.heading = 270; m.nav_status = 0;
    std::snprintf(m.name, sizeof(m.name), "NEDERLAND");
    std::snprintf(m.callsign, sizeof(m.callsign), "PA1234");
    m.ship_type = 70;
    JsonWriter w;
    to_json(m, w);
    EXPECT_EQ(w.out,
        "{\"mmsi\":244660000,\"type\":1,\"lat\":52.375,\"lon\":4.9,\"sog\":12.5,\"cog\":271.3,"
        "\"heading\":270,\"nav_status\":0,\"name\":\"NEDERLAND\",\"callsign\":\"PA1234\",\"ship_type\":70}");

    ais::Message bare{};
    bare.type = 5; bare.mmsi = 1; bare.sog = -1.0f; bare.cog = -1.0f;
    JsonWriter w2;
    to_json(bare, w2);
    EXPECT_EQ(w2.out, "{\"mmsi\":1,\"type\":5}");
}

TEST(JsonAdapters, AprsPacketToJsonEscapesText) {
    aprs::Packet p{};
    std::snprintf(p.source, sizeof(p.source), "4X1RF-9");
    std::snprintf(p.dest, sizeof(p.dest), "APCLER");
    std::snprintf(p.path, sizeof(p.path), "WIDE1-1");
    p.type = '!'; p.has_position = true; p.lat = 32.25; p.lon = 35.0;
    p.course = 90.0f; p.speed = 5.0f; p.symbol_table = '/'; p.symbol_code = '>';
    std::snprintf(p.comment, sizeof(p.comment), "say \"hi\"\tnow");
    JsonWriter w;
    to_json(p, w);
    EXPECT_EQ(w.out,
        "{\"source\":\"4X1RF-9\",\"dest\":\"APCLER\",\"path\":\"WIDE1-1\",\"type\":\"!\","
        "\"lat\":32.25,\"lon\":35,\"course\":90,\"speed\":5,\"symbol\":\"/>\","
        "\"comment\":\"say \\\"hi\\\"\\tnow\"}");
}

TEST(JsonAdapters, RdsStationToJson) {
    rds::Station s{};
    s.synced = true; s.pi = 0x4416; s.pty = 11; s.tp = true;
    std::snprintf(s.ps, sizeof(s.ps), "K-BARAMA");
    std::snprintf(s.rt, sizeof(s.rt), "now playing");
    s.groups_ok = 40; s.blocks_total = 200; s.blocks_corrected = 10; s.blocks_bad = 50;
    JsonWriter w;
    to_json(s, w);
    EXPECT_EQ(w.out,
        "{\"synced\":true,\"pi\":17430,\"pty\":11,\"tp\":true,\"ta\":false,"
        "\"ps\":\"K-BARAMA\",\"rt\":\"now playing\",\"groups_ok\":40,"
        "\"corrected_pct\":5,\"bad_pct\":25}");
}

// the writer's buffer is reused, so a long run must not keep growing it

TEST(JsonAdapters, TextSinkReusesItsBuffer) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    JsonTextSinkBlock<ais::Message> sink("ais json", srv, "ais", 512);

    ais::Message m{};
    m.type = 1; m.has_position = true; m.lat = 52.0; m.lon = 4.0;
    std::snprintf(m.name, sizeof(m.name), "SHIP");
    EXPECT_TRUE(sink.procedure().is_err());

    size_t capacity_after_warmup = 0;
    for (int round = 0; round < 40; ++round) {
        for (int i = 0; i < 256; ++i) { m.mmsi = 200000000u + i; sink.in.push(m); }
        ASSERT_TRUE(sink.procedure().is_ok());
        EXPECT_EQ(sink.in.size(), 0u);
        if (round == 0) capacity_after_warmup = sink.buffer_capacity();
        else EXPECT_EQ(sink.buffer_capacity(), capacity_after_warmup);
    }
}

namespace {

// One APRS packet as NBFM IQ at 48 kHz: optional carrier offset, noise, and an
// adjacent 2 m signal of the kind that survives the 240 -> 48 kHz resampler.
std::vector<std::complex<float>> aprs_nbfm_iq(double offset_hz, float noise,
                                              double interferer_hz, float interferer_amp) {
    uint8_t frame[64];
    const size_t n = aprs::encode_ui("APCLER", "4X1RF-9", "WIDE1-1", "!3249.23N/03500.62E>hi", frame, sizeof(frame));
    std::array<bool, 4096> tx{};
    const size_t nb = aprs::encode_frame(frame, n, tx.data(), tx.size(), 24);

    const double fs = 48e3, baud = 1200.0, dev = 5e3;
    const size_t sps = static_cast<size_t>(fs / baud);
    std::vector<float> audio(sps * 20, 0.0f);
    double ph = 0.0;
    for (size_t i = 0; i < nb; ++i) {
        const double f = tx[i] ? 1200.0 : 2200.0;
        for (size_t k = 0; k < sps; ++k) { audio.push_back(0.5f * static_cast<float>(std::sin(ph))); ph += 2.0 * M_PI * f / fs; }
    }
    audio.insert(audio.end(), sps * 20, 0.0f);

    std::srand(1234);
    std::vector<std::complex<float>> iq(audio.size());
    double cph = 0.0, oph = 0.0, iph = 0.0, itone = 0.0;
    for (size_t i = 0; i < audio.size(); ++i) {
        cph += 2.0 * M_PI * dev * audio[i] / fs;
        oph += 2.0 * M_PI * offset_hz / fs;
        std::complex<float> s{static_cast<float>(std::cos(cph + oph)), static_cast<float>(std::sin(cph + oph))};
        if (interferer_amp > 0.0f) {
            itone += 2.0 * M_PI * 900.0 / fs;                       // the neighbour is talking too
            iph += 2.0 * M_PI * (interferer_hz + dev * 0.8 * std::sin(itone)) / fs;
            s += interferer_amp * std::complex<float>(static_cast<float>(std::cos(iph)), static_cast<float>(std::sin(iph)));
        }
        if (noise > 0.0f) {
            s += noise * std::complex<float>(std::rand() / static_cast<float>(RAND_MAX) - 0.5f,
                                             std::rand() / static_cast<float>(RAND_MAX) - 0.5f);
        }
        iq[i] = s;
    }
    return iq;
}

// Runs the real tail of the APRS tap; with_filter mirrors what websdr wires up.
void run_aprs_tap(const std::vector<std::complex<float>>& iq, bool with_filter, WebServer& srv) {
    const double fs = 48e3, dev = 5e3;
    KaiserLPFBlock<std::complex<float>> lpf("chan", fs, 7.5e3, 3e3, 60.0, 1 << 16);
    FMDemodBlock fm("nbfm", fs, dev, 1 << 16);
    AFSKDemodBlock afsk("afsk", fs, 1 << 14);
    JsonTextSinkBlock<aprs::Packet> json("aprs json", srv, "aprs", 256);
    cler::Channel<std::complex<float>> filtered(1 << 14);
    cler::Channel<float> demodulated(1 << 14);

    auto pump_tail = [&]() {
        while (fm.procedure(&demodulated).is_ok()) {
            auto [r, rs] = demodulated.read_dbf();
            auto [aw, aws] = afsk.in.write_dbf();
            const size_t m = std::min(rs, aws);
            for (size_t i = 0; i < m; ++i) aw[i] = r[i];
            afsk.in.commit_write(m);
            demodulated.commit_read(m);
            while (afsk.procedure(&json.in).is_ok()) {}
            while (json.procedure().is_ok()) {}
        }
    };
    auto pump_filter = [&]() {
        while (lpf.procedure(&filtered).is_ok()) {
            auto [r, rs] = filtered.read_dbf();
            auto [fw, fws] = fm.in.write_dbf();
            const size_t m = std::min(rs, fws);
            for (size_t i = 0; i < m; ++i) fw[i] = r[i];
            fm.in.commit_write(m);
            filtered.commit_read(m);
            pump_tail();
        }
    };

    size_t pos = 0;
    while (pos < iq.size()) {
        cler::Channel<std::complex<float>>* head = with_filter ? &lpf.in : &fm.in;
        auto [w, ws] = head->write_dbf();
        const size_t take = std::min(ws, iq.size() - pos);
        for (size_t i = 0; i < take; ++i) w[i] = iq[pos + i];
        head->commit_write(take);
        pos += take;
        if (with_filter) pump_filter();
        pump_tail();
    }
    if (with_filter) pump_filter();
    pump_tail();
    while (afsk.procedure(&json.in).is_ok()) {}
    while (json.procedure().is_ok()) {}
}

}  // namespace

// The APRS tap carries its own NBFM demodulator, so what the listener is tuned
// to in AM or SSB makes no difference to it.

TEST(JsonAdapters, AprsTapDecodesWithoutTheListenersDemod) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.start();
    TestClient c("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(c.wait([&] { return c.open; }));

    run_aprs_tap(aprs_nbfm_iq(0.0, 0.0f, 0.0, 0.0f), true, srv);

    ASSERT_TRUE(c.wait([&] { return !c.text_with("text").empty(); }, 3000))
        << "no APRS packet reached the text stream";
    const std::string got = c.text_with("text");
    EXPECT_NE(got.find("\"stream\":\"aprs\""), std::string::npos) << got;
    EXPECT_NE(got.find("\"source\":\"4X1RF-9\""), std::string::npos) << got;
    srv.stop();
}

// Off the air the packet is off frequency, noisy, and shares the band with a
// louder neighbour that the resampler alone does not remove.
TEST(JsonAdapters, AprsTapSurvivesOffsetNoiseAndAnAdjacentSignal) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.start();
    TestClient c("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(c.wait([&] { return c.open; }));

    run_aprs_tap(aprs_nbfm_iq(1.5e3, 0.05f, 15e3, 2.0f), true, srv);

    ASSERT_TRUE(c.wait([&] { return !c.text_with("text").empty(); }, 3000))
        << "the channel filter should have kept the neighbour out";
    EXPECT_NE(c.text_with("text").find("\"source\":\"4X1RF-9\""), std::string::npos);
    srv.stop();

    // and without that filter the louder neighbour captures the discriminator
    const int bare_port = free_port();
    ServerOptions bo; bo.port = bare_port;
    WebServer bare(bo);
    bare.start();
    TestClient bc("ws://127.0.0.1:" + std::to_string(bare_port) + "/");
    ASSERT_TRUE(bc.wait([&] { return bc.open; }));
    run_aprs_tap(aprs_nbfm_iq(1.5e3, 0.05f, 15e3, 2.0f), false, bare);
    EXPECT_FALSE(bc.wait([&] { return !bc.text_with("text").empty(); }, 1000))
        << "unfiltered path decoded anyway: this test no longer proves the filter";
    bare.stop();
}
