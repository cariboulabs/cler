#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXHttpClient.h>
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/web_sink.hpp"
#include "desktop_blocks/sigmf/recorder_sigmf.hpp"
#include <filesystem>
#include <string>

using namespace web;

TEST(WebProto, SpectrumRoundTrip) {
    SpectrumFrame f{};
    f.gen = 7; f.center_hz = 100e6; f.rate_hz = 2.4e6; f.n = 1024; f.db_min = -120.0f; f.db_step = 0.5f;
    for (int i = 0; i < f.n; ++i) f.bins[i] = static_cast<uint8_t>(i);
    std::vector<uint8_t> buf(8192);
    const size_t len = encode_spectrum(f, 42, buf.data(), buf.size());
    ASSERT_EQ(len, SPECTRUM_HEAD_BYTES + 1024u);
    EXPECT_EQ(buf[0], T_SPECTRUM);
    EXPECT_EQ(buf[1], PROTO_VER);
    EXPECT_EQ(buf[2], 7u); EXPECT_EQ(buf[3], 0u);
    EXPECT_EQ(buf[6], 42u);
    Header h; SpectrumFrame g{};
    ASSERT_TRUE(decode_spectrum(buf.data(), len, h, g));
    EXPECT_EQ(h.gen, 7u); EXPECT_EQ(h.seq, 42u);
    EXPECT_DOUBLE_EQ(g.center_hz, 100e6); EXPECT_DOUBLE_EQ(g.rate_hz, 2.4e6);
    EXPECT_EQ(g.n, 1024); EXPECT_FLOAT_EQ(g.db_min, -120.0f); EXPECT_FLOAT_EQ(g.db_step, 0.5f);
    EXPECT_EQ(g.bins[1000], 1000 & 0xFF);
    EXPECT_FALSE(decode_spectrum(buf.data(), len - 1, h, g));
    EXPECT_EQ(encode_spectrum(f, 1, buf.data(), 100), 0u);
}

TEST(WebProto, AudioRoundTrip) {
    std::vector<int16_t> pcm(AUDIO_CHUNK);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(i * 7 - 3000);
    std::vector<uint8_t> buf(4096);
    const size_t len = encode_audio(3, 9, pcm.data(), pcm.size(), buf.data(), buf.size());
    ASSERT_EQ(len, AUDIO_HEAD_BYTES + 2 * AUDIO_CHUNK);
    Header h; uint8_t codec = 99; std::vector<int16_t> out(AUDIO_CHUNK); size_t n = 0;
    ASSERT_TRUE(decode_audio(buf.data(), len, h, codec, out.data(), out.size(), n));
    EXPECT_EQ(h.type, T_AUDIO); EXPECT_EQ(h.gen, 3u); EXPECT_EQ(h.seq, 9u);
    EXPECT_EQ(codec, CODEC_PCM16_48K); EXPECT_EQ(n, AUDIO_CHUNK);
    EXPECT_EQ(out, pcm);
    buf[1] = 2;
    EXPECT_FALSE(decode_header(buf.data(), len, h));
}

TEST(WebProto, JsonWriterAndParser) {
    JsonWriter w;
    w.begin_obj().key("t").str("he\"llo\n").key("n").num(1.5).key("b").boolean(true)
     .key("a").begin_arr().num(1).num(2).end().key("o").begin_obj().key("x").raw("null").end().end();
    EXPECT_EQ(w.out, "{\"t\":\"he\\\"llo\\n\",\"n\":1.5,\"b\":true,\"a\":[1,2],\"o\":{\"x\":null}}");
    Fields f;
    ASSERT_TRUE(json_parse_object(w.out, f));
    EXPECT_EQ(json_str(f, "t"), "he\"llo\n");
    EXPECT_DOUBLE_EQ(json_num(f, "n"), 1.5);
    EXPECT_EQ(*json_find(f, "a"), "[1,2]");
    EXPECT_EQ(*json_find(f, "o"), "{\"x\":null}");
    EXPECT_EQ(json_str(f, "missing", "d"), "d");
    EXPECT_TRUE(json_parse_object("{}", f));
    EXPECT_FALSE(json_parse_object("{\"t\":}", f));
    EXPECT_FALSE(json_parse_object("{\"t\":\"x\"", f));
    EXPECT_FALSE(json_parse_object("[1]", f));
    EXPECT_FALSE(json_parse_object("{t:1}", f));
}

namespace {

int free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    ::close(s);
    return ntohs(a.sin_port);
}

struct TestClient {
    ix::WebSocket ws;
    std::recursive_mutex m;
    std::condition_variable_any cv;
    std::vector<std::string> texts;
    std::vector<std::string> bins;
    bool open = false, closed = false;
    uint16_t close_code = 0;

    explicit TestClient(const std::string& url, const std::string& origin = "") {
        ws.setUrl(url);
        ws.disableAutomaticReconnection();
        ws.disablePerMessageDeflate();
        if (!origin.empty()) { ix::WebSocketHttpHeaders h; h["Origin"] = origin; ws.setExtraHeaders(h); }
        ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            std::lock_guard<std::recursive_mutex> lock(m);
            if (msg->type == ix::WebSocketMessageType::Open) open = true;
            else if (msg->type == ix::WebSocketMessageType::Close) { closed = true; close_code = msg->closeInfo.code; }
            else if (msg->type == ix::WebSocketMessageType::Error) closed = true;
            else if (msg->type == ix::WebSocketMessageType::Message) (msg->binary ? bins : texts).push_back(msg->str);
            cv.notify_all();
        });
        ws.start();
    }
    ~TestClient() { ws.stop(); }
    template <typename F> bool wait(F pred, int ms = 3000) {
        std::unique_lock<std::recursive_mutex> lock(m);
        return cv.wait_for(lock, std::chrono::milliseconds(ms), pred);
    }
    std::string text_with(const char* t) {
        std::lock_guard<std::recursive_mutex> lock(m);
        for (size_t i = texts.size(); i-- > 0;) if (texts[i].find(std::string("\"t\":\"") + t + "\"") != std::string::npos) return texts[i];
        return "";
    }
};

}

TEST(WebServerTest, HelloRolesStateAndControl) {
    const int port = free_port();
    ServerOptions o; o.port = port; o.version = "t1";
    WebServer srv(o);
    srv.set_hello_extra("\"sources\":[{\"id\":\"sim\"}]");
    srv.set_state("{\"gen\":1,\"freq\":100e6}");
    srv.start();

    TestClient a("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(a.wait([&] { return !a.texts.empty(); }));
    const std::string hello = a.text_with("hello");
    EXPECT_NE(hello.find("\"proto\":1"), std::string::npos);
    EXPECT_NE(hello.find("\"role\":\"ctl\""), std::string::npos);
    EXPECT_NE(hello.find("\"sources\":[{\"id\":\"sim\"}]"), std::string::npos);
    EXPECT_NE(hello.find("\"state\":{\"gen\":1,\"freq\":100e6}"), std::string::npos);

    TestClient b("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(b.wait([&] { return !b.texts.empty(); }));
    EXPECT_NE(b.text_with("hello").find("\"role\":\"view\""), std::string::npos);
    EXPECT_EQ(srv.client_count(), 2u);

    b.ws.sendText("{\"t\":\"set\",\"freq\":1e6}");
    ASSERT_TRUE(b.wait([&] { return !b.text_with("error").empty(); }));
    EXPECT_NE(b.text_with("error").find("view"), std::string::npos);
    std::string ctl;
    EXPECT_FALSE(srv.pop_control(ctl));

    a.ws.sendText("{\"t\":\"set\",\"freq\":1e6}");
    for (int i = 0; i < 100 && !srv.pop_control(ctl); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(ctl, "{\"t\":\"set\",\"freq\":1e6}");

    a.ws.sendText("not json");
    ASSERT_TRUE(a.wait([&] { return !a.text_with("error").empty(); }));

    srv.set_state("{\"gen\":2,\"freq\":1e6}");
    ASSERT_TRUE(a.wait([&] { return !a.text_with("state").empty(); }));
    EXPECT_NE(a.text_with("state").find("\"gen\":2"), std::string::npos);
    EXPECT_NE(a.text_with("state").find("\"role\":\"ctl\""), std::string::npos);

    a.ws.stop();
    ASSERT_TRUE(b.wait([&] { return b.text_with("state").find("\"role\":\"ctl\"") != std::string::npos; }));
    EXPECT_NE(b.text_with("state").find("\"gen\":2"), std::string::npos);
    srv.stop();
}

TEST(WebServerTest, OriginAndTokenRejected) {
    const int port = free_port();
    ServerOptions o; o.port = port; o.token = "s3cret";
    WebServer srv(o);
    srv.start();
    const std::string base = "ws://127.0.0.1:" + std::to_string(port) + "/";

    TestClient bad_origin(base + "?token=s3cret", "http://evil.example:1234");
    ASSERT_TRUE(bad_origin.wait([&] { return bad_origin.closed; }));
    EXPECT_TRUE(bad_origin.texts.empty());

    TestClient no_token(base);
    ASSERT_TRUE(no_token.wait([&] { return no_token.closed; }));
    EXPECT_TRUE(no_token.texts.empty());

    TestClient ok(base + "?token=s3cr%65t", "http://127.0.0.1:" + std::to_string(port));
    ASSERT_TRUE(ok.wait([&] { return !ok.texts.empty(); }));
    EXPECT_NE(ok.text_with("hello").find("\"role\":\"ctl\""), std::string::npos);

    ix::HttpClient http;
    EXPECT_EQ(http.get("http://127.0.0.1:" + std::to_string(port) + "/health", http.createRequest())->statusCode, 401);
    EXPECT_EQ(http.get("http://127.0.0.1:" + std::to_string(port) + "/health?token=s3cret", http.createRequest())->statusCode, 200);
    EXPECT_EQ(http.get("http://127.0.0.1:" + std::to_string(port) + "/", http.createRequest())->statusCode, 404);  // no client files in this test, but not 401
    srv.stop();
}

TEST(WebServerTest, RebindingHostRejectedWithoutToken) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.start();
    const std::string hp = "evil.example:" + std::to_string(port);
    ix::WebSocket ws;
    ws.setUrl("ws://127.0.0.1:" + std::to_string(port) + "/");
    ix::WebSocketHttpHeaders h; h["Host"] = hp; h["Origin"] = "http://" + hp;
    ws.setExtraHeaders(h);
    ws.disableAutomaticReconnection();
    std::mutex m; std::condition_variable cv; bool closed = false; uint16_t code = 0; int texts = 0;
    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        std::lock_guard<std::mutex> lock(m);
        if (msg->type == ix::WebSocketMessageType::Close) { closed = true; code = msg->closeInfo.code; }
        if (msg->type == ix::WebSocketMessageType::Message) ++texts;
        cv.notify_all();
    });
    ws.start();
    std::unique_lock<std::mutex> lock(m);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return closed; }));
    EXPECT_EQ(code, 1008);
    EXPECT_EQ(texts, 0);
    lock.unlock();
    ws.stop();
    srv.stop();
}

TEST(WebServerTest, StreamsArriveAndDecode) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.start();
    TestClient c("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(c.wait([&] { return c.open; }));

    SpectrumFrame f{}; f.gen = 5; f.center_hz = 1e6; f.rate_hz = 2e6; f.n = 64; f.db_min = -100; f.db_step = 1;
    for (int i = 0; i < 64; ++i) f.bins[i] = static_cast<uint8_t>(i * 3);
    srv.set_gen(5);
    EXPECT_TRUE(srv.push_spectrum(f));
    std::vector<int16_t> pcm(AUDIO_CHUNK * 3);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(i);
    EXPECT_EQ(srv.push_audio(pcm.data(), pcm.size()), pcm.size());
    srv.push_text("rds", "{\"ps\":\"TEST\"}");

    ASSERT_TRUE(c.wait([&] { return c.bins.size() >= 4 && !c.text_with("text").empty(); }));
    int spectra = 0, audios = 0;
    for (auto& b : c.bins) {
        const auto* p = reinterpret_cast<const uint8_t*>(b.data());
        Header h; ASSERT_TRUE(decode_header(p, b.size(), h));
        if (h.type == T_SPECTRUM) {
            SpectrumFrame g{}; ASSERT_TRUE(decode_spectrum(p, b.size(), h, g));
            EXPECT_EQ(g.n, 64); EXPECT_EQ(g.bins[10], 30); EXPECT_EQ(h.gen, 5u); ++spectra;
        } else {
            uint8_t codec; std::vector<int16_t> out(AUDIO_CHUNK); size_t n;
            ASSERT_TRUE(decode_audio(p, b.size(), h, codec, out.data(), out.size(), n));
            EXPECT_EQ(n, AUDIO_CHUNK); EXPECT_EQ(h.gen, 5u); EXPECT_EQ(out[0], static_cast<int16_t>(h.seq * AUDIO_CHUNK)); ++audios;
        }
    }
    EXPECT_EQ(spectra, 1); EXPECT_EQ(audios, 3);
    EXPECT_NE(c.text_with("text").find("\"stream\":\"rds\",\"data\":{\"ps\":\"TEST\"}"), std::string::npos);
    ASSERT_TRUE(c.wait([&] { return !c.text_with("stats").empty(); }, 5000));

    ix::HttpClient http;
    auto r = http.get("http://127.0.0.1:" + std::to_string(port) + "/health", http.createRequest());
    EXPECT_EQ(r->statusCode, 200);
    EXPECT_NE(r->body.find("\"clients\":1"), std::string::npos);
    auto nf = http.get("http://127.0.0.1:" + std::to_string(port) + "/client/../x", http.createRequest());
    EXPECT_EQ(nf->statusCode, 404);
    srv.stop();
}

TEST(WebSink, DrainsWithoutClientsAndConvertsAudio) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    WebSinkBlock sink("sink", srv);
    SpectrumFrame f{}; f.n = 8;
    std::vector<float> a(5000, 0.5f);
    sink.audio.writeN(a.data(), a.size());
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 8; ++i) sink.spectrum.push(f);
        EXPECT_TRUE(sink.procedure().is_ok());
        EXPECT_EQ(sink.spectrum.size(), 0u);
    }
    EXPECT_EQ(sink.audio.size(), 0u);
    const auto d = srv.total_dropped();
    EXPECT_GE(d.spectrum_dropped, 1u);
    EXPECT_LE(d.spectrum_dropped, 9u);
    EXPECT_EQ(d.audio_dropped, 0u);
    EXPECT_TRUE(sink.procedure().is_err());
}

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
    const int port = free_port();
    ServerOptions o; o.port = port; o.record_dir = dir; o.token = "s3";
    WebServer srv(o);
    srv.start();
    const std::string root = "http://127.0.0.1:" + std::to_string(port);
    ix::HttpClient http;

    EXPECT_EQ(http.get(root + "/recordings", http.createRequest())->statusCode, 401);
    auto list = http.get(root + "/recordings?token=s3", http.createRequest());
    ASSERT_EQ(list->statusCode, 200);
    EXPECT_NE(list->body.find("\"name\":\"take1\""), std::string::npos);
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
