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
#include "tests/web_test_client.hpp"
#include <ixwebsocket/IXHttpClient.h>
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/web_sink.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace web;
using webtest::free_port;
using webtest::TestClient;

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

TEST(WebServerTest, HelloRolesStateAndControl) {
    const int port = free_port();
    ServerOptions o; o.port = port; o.version = "t1";
    WebServer srv(o);
    srv.set_hello_extra("{\"sources\":[{\"id\":\"sim\"}]}");
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
    srv.add_http_route("/health", [](const std::string&, const std::string&) {
        return HttpReply{200, "{}", "application/json"};
    });
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

// `ssh -L 8080:localhost:8080` resolves to ::1 first on a stock Debian, so an
// IPv4-only listener makes the obvious tunnel hang.
TEST(WebServerTest, LoopbackServesBothFamilies) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.add_http_route("/health", [](const std::string&, const std::string&) {
        return HttpReply{200, "{}", "application/json"};
    });
    srv.start();
    ix::HttpClient http;
    EXPECT_EQ(http.get("http://127.0.0.1:" + std::to_string(port) + "/health", http.createRequest())->statusCode, 200);
    // ix::HttpClient cannot parse a bracketed IPv6 URL, so ask ::1 over a raw socket
    EXPECT_EQ(webtest::http_status_v6(port, "/health"), 200);
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
    srv.add_http_route("/health", [&srv](const std::string&, const std::string&) {
        JsonWriter w;
        w.begin_obj().key("clients").num(srv.client_count()).end();
        return HttpReply{200, w.out, "application/json"};
    });
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

// Every frame splices app-supplied objects into a server-owned one; a missed
// empty-guard used to produce `{,"x":1}` and nothing noticed.
TEST(WebServerTest, ComposedFramesAreValidJsonWithAndWithoutExtras) {
    for (const bool extras : {false, true}) {
        const int port = free_port();
        ServerOptions o; o.port = port; o.version = "compose";
        WebServer srv(o);
        if (extras) {
            srv.set_hello_extra("{\"sources\":[{\"id\":\"sim\"}],\"decoders\":[]}");
            srv.set_state("{\"gen\":7,\"freq\":100e6}");
            srv.set_stats_extra("{\"overflows\":3}");
        } else {
            srv.set_hello_extra("{}");
            srv.set_stats_extra("");
        }
        srv.add_http_route("/health", [&srv, extras](const std::string&, const std::string&) {
            JsonWriter w;
            w.begin_obj().key("version").str("compose").key("clients").num(srv.client_count());
            if (extras) w.key("source").str("sim");
            w.end();
            return HttpReply{200, w.out, "application/json"};
        });
        srv.start();

        TestClient c("ws://127.0.0.1:" + std::to_string(port) + "/");
        ASSERT_TRUE(c.wait([&] { return !c.text_with("hello").empty(); }));
        srv.push_text("rds", extras ? "{\"ps\":\"K-BA\"}" : "");
        srv.send_error("set", "quote\" and \\ backslash");
        srv.set_state(extras ? "{\"gen\":8}" : "{}");
        ASSERT_TRUE(c.wait([&] { return !c.text_with("stats").empty() && !c.text_with("text").empty() &&
                                        !c.text_with("error").empty() && !c.text_with("state").empty(); }, 5000));

        for (const char* t : {"hello", "state", "stats", "text", "error"}) {
            const std::string frame = c.text_with(t);
            ASSERT_FALSE(frame.empty()) << t << " extras=" << extras;
            Fields f;
            EXPECT_TRUE(json_parse_object(frame, f)) << t << " extras=" << extras << " -> " << frame;
            EXPECT_EQ(json_str(f, "t"), t) << frame;
        }
        ix::HttpClient http;
        auto r = http.get("http://127.0.0.1:" + std::to_string(port) + "/health", http.createRequest());
        ASSERT_EQ(r->statusCode, 200);
        Fields hf;
        EXPECT_TRUE(json_parse_object(r->body, hf)) << r->body;
        EXPECT_EQ(json_str(hf, "version"), "compose");
        srv.stop();
    }
}

// A client can only put the reason under the right device row if the frame says
// which device it is about.
TEST(WebServerTest, SourceErrorsNameTheDeviceAndTheReason) {
    const int port = free_port();
    ServerOptions o; o.port = port;
    WebServer srv(o);
    srv.start();
    TestClient c("ws://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(c.wait([&] { return !c.text_with("hello").empty(); }));

    srv.send_error("source", "no access to /dev/gpiomem", "cariboulite:s1g");
    ASSERT_TRUE(c.wait([&] { return !c.text_with("error").empty(); }));
    Fields f;
    const std::string frame = c.text_with("error");
    ASSERT_TRUE(json_parse_object(frame, f)) << frame;
    EXPECT_EQ(json_str(f, "code"), "source");
    EXPECT_EQ(json_str(f, "id"), "cariboulite:s1g");
    EXPECT_FALSE(json_str(f, "msg").empty());

    // an error about nothing in particular carries no id to attach it to
    srv.send_error("record", "disk almost full");
    ASSERT_TRUE(c.wait([&] {
        return c.text_with("error").find("disk almost full") != std::string::npos;
    }));
    Fields g;
    ASSERT_TRUE(json_parse_object(c.text_with("error"), g));
    EXPECT_TRUE(json_str(g, "id").empty());
    srv.stop();
}

TEST(WebProto, FieldsOfSplicesOnlyNonEmptyObjects) {
    JsonWriter w;
    w.begin_obj().key("a").num(1).fields_of("").fields_of("{}").fields_of("{ }").fields_of("{\n\t}")
     .fields_of("{\"b\":2}").fields_of("{\"c\":3}").end();
    EXPECT_EQ(w.out, "{\"a\":1,\"b\":2,\"c\":3}");
    JsonWriter lead;
    lead.begin_obj().fields_of("{\"only\":true}").end();
    EXPECT_EQ(lead.out, "{\"only\":true}");
    // anything that is not exactly an object contributes nothing, so trailing
    // junk cannot ride into a frame
    for (const char* bad : {"{\"b\":\"x\"} junk", "[1,2]", "\"str\"", "{\"b\":1", "not json"}) {
        JsonWriter j;
        j.begin_obj().key("a").num(1).fields_of(bad).end();
        EXPECT_EQ(j.out, "{\"a\":1}") << bad;
    }
    Fields f;
    EXPECT_TRUE(json_parse_object(w.out, f));
}

// %.10g silently rounded these; a byte count passes 10 digits after ~9 minutes
// at 2.4 MS/s and a drop counter can go anywhere.
TEST(WebProto, IntegerFieldsKeepEveryDigit) {
    const uint64_t big = 12345678901234ull;   // 14 digits, > 10 GB
    JsonWriter w;
    w.begin_obj().key("bytes").num(big).key("small").num(size_t{7})
     .key("neg").num(int64_t{-9007199254740993ll}).key("real").num(1.5).end();
    EXPECT_EQ(w.out, "{\"bytes\":12345678901234,\"small\":7,\"neg\":-9007199254740993,\"real\":1.5}");
    Fields f;
    ASSERT_TRUE(json_parse_object(w.out, f));
    EXPECT_EQ(*json_find(f, "bytes"), "12345678901234");
    EXPECT_EQ(*json_find(f, "neg"), "-9007199254740993");
}

TEST(WebServerDeathTest, RouteRegistrationRejectsShadowingAndLateCalls) {
    ServerOptions o; o.port = free_port();
    WebServer srv(o);
    auto ok = [](const std::string&, const std::string&) { return HttpReply{200, "{}", "application/json"}; };
    for (const char* bad : {"", "/", "health", "/client", "/health/"}) {
        EXPECT_DEATH(srv.add_http_route(bad, ok), "") << bad;
    }
    srv.add_http_route("/health", ok);
    EXPECT_DEATH(srv.add_http_route("/health", ok), "");
    srv.start();
    EXPECT_DEATH(srv.add_http_route("/late", ok), "");
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
