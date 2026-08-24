#include <gtest/gtest.h>

#include "desktop_examples/cler_connector/connector_net.hpp"
#include "desktop_examples/cler_connector/connector_proto.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<char*> argv_of(const std::vector<std::string>& args, std::vector<std::string>& storage) {
    storage = args;
    std::vector<char*> out;
    for (auto& s : storage) out.push_back(s.data());
    return out;
}

conn::Options parse(const std::vector<std::string>& args) {
    std::vector<std::string> storage;
    auto v = argv_of(args, storage);
    return conn::parse_args(static_cast<int>(v.size()), v.data());
}

int connect_loopback(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return -1;
    }
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

// The connector streams continuously, so a reader that attaches between two
// pushes simply starts later; keep pushing until the bytes turn up.
bool drain(int fd, void* out, size_t bytes, conn::IqServer& iq,
           const std::complex<float>* block, size_t block_len) {
    size_t have = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (have < bytes && std::chrono::steady_clock::now() < deadline) {
        iq.push(block, block_len);
        const ssize_t n = ::recv(fd, static_cast<char*>(out) + have, bytes - have, 0);
        if (n > 0) have += static_cast<size_t>(n);
    }
    return have == bytes;
}

template <typename F>
bool wait_for(F&& pred, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

}  // namespace

TEST(ConnectorArgs, DefaultsMatchUpstream) {
    const auto o = parse({"cler_connector"});
    EXPECT_TRUE(o.error.empty());
    EXPECT_EQ(o.iq_port, 4950);
    EXPECT_EQ(o.control_port, -1);
    EXPECT_EQ(o.rtltcp_port, -1);
    EXPECT_EQ(o.samp_rate, 0);
    EXPECT_EQ(o.frequency, 0);
    EXPECT_EQ(o.gain, "auto");
    EXPECT_FALSE(o.iqswap);
}

TEST(ConnectorArgs, EveryFlagInTheOrderOwrxSends) {
    const auto o = parse({"cler_connector", "-s", "2400000", "-f", "100000000", "-p", "4590", "-c", "4591",
                          "-d", "hackrf:abc", "-P", "1.5", "-g", "LNA=24,VGA=20", "-a", "RX2",
                          "-t", "bias_tx=true", "-i", "-r", "1234"});
    EXPECT_TRUE(o.error.empty());
    EXPECT_EQ(o.samp_rate, 2400000);
    EXPECT_EQ(o.frequency, 100000000);
    EXPECT_EQ(o.iq_port, 4590);
    EXPECT_EQ(o.control_port, 4591);
    EXPECT_EQ(o.device, "hackrf:abc");
    EXPECT_DOUBLE_EQ(o.ppm, 1.5);
    EXPECT_EQ(o.gain, "LNA=24,VGA=20");
    EXPECT_EQ(o.antenna, "RX2");
    EXPECT_EQ(o.settings, "bias_tx=true");
    EXPECT_TRUE(o.iqswap);
    EXPECT_EQ(o.rtltcp_port, 1234);
}

TEST(ConnectorArgs, LongFormsAndInlineValues) {
    const auto o = parse({"cler_connector", "--port=4950", "--control", "4951", "--device=sim", "--version"});
    EXPECT_TRUE(o.error.empty());
    EXPECT_EQ(o.iq_port, 4950);
    EXPECT_EQ(o.control_port, 4951);
    EXPECT_EQ(o.device, "sim");
    EXPECT_TRUE(o.version);
}

TEST(ConnectorArgs, BadInputIsReportedNotGuessed) {
    EXPECT_FALSE(parse({"cler_connector", "--nope"}).error.empty());
    EXPECT_FALSE(parse({"cler_connector", "-p"}).error.empty());
    EXPECT_FALSE(parse({"cler_connector", "extra"}).error.empty());
}

TEST(ConnectorGain, EveryFormOfTheSpec) {
    for (const char* automatic : {"auto", "AUTO", "none", "None", ""}) {
        const auto g = conn::parse_gain(automatic);
        EXPECT_TRUE(g.automatic) << automatic;
        EXPECT_FALSE(g.single);
    }
    const auto single = conn::parse_gain("31.5");
    EXPECT_FALSE(single.automatic);
    EXPECT_TRUE(single.single);
    EXPECT_DOUBLE_EQ(single.value, 31.5);

    const auto pairs = conn::parse_gain("LNA=24,VGA=20");
    EXPECT_FALSE(pairs.automatic);
    EXPECT_FALSE(pairs.single);
    ASSERT_EQ(pairs.pairs.size(), 2u);
    EXPECT_EQ(pairs.pairs[0].first, "LNA");
    EXPECT_DOUBLE_EQ(pairs.pairs[0].second, 24);
    EXPECT_EQ(pairs.pairs[1].first, "VGA");
    EXPECT_DOUBLE_EQ(pairs.pairs[1].second, 20);

    EXPECT_TRUE(conn::parse_gain("garbage").automatic);
}

TEST(ConnectorLines, ReassemblesAcrossAnyChunkBoundary) {
    const std::string stream = "center_freq:100000000\nrf_gain:auto\nsamp_rate:2400000\niqswap:true\npartial:le";
    for (size_t chunk = 1; chunk <= stream.size(); ++chunk) {
        conn::LineReader reader;
        std::vector<std::string> lines;
        for (size_t at = 0; at < stream.size(); at += chunk) {
            const size_t n = std::min(chunk, stream.size() - at);
            reader.feed(stream.data() + at, n, [&](const std::string& l) { lines.push_back(l); });
        }
        ASSERT_EQ(lines.size(), 4u) << "chunk " << chunk;
        EXPECT_EQ(lines[0], "center_freq:100000000");
        EXPECT_EQ(lines[3], "iqswap:true");
        EXPECT_EQ(reader.buf, "partial:le") << "chunk " << chunk;
    }
}

TEST(ConnectorLines, CrLfAndOverlongGarbage) {
    conn::LineReader reader;
    std::vector<std::string> lines;
    reader.feed("ppm:0\r\n", 7, [&](const std::string& l) { lines.push_back(l); });
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "ppm:0");

    const std::string flood(conn::LineReader::MAX_LINE + 10, 'x');
    reader.feed(flood.data(), flood.size(), [&](const std::string& l) { lines.push_back(l); });
    EXPECT_EQ(lines.size(), 1u);
    EXPECT_LE(reader.buf.size(), conn::LineReader::MAX_LINE);
    reader.feed("\ncenter_freq:7\n", 15, [&](const std::string& l) { lines.push_back(l); });
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[1], "center_freq:7");
}

TEST(ConnectorLines, KeyValueSplitAndTruthy) {
    std::string k, v;
    ASSERT_TRUE(conn::split_kv("center_freq:100e6", k, v));
    EXPECT_EQ(k, "center_freq");
    EXPECT_EQ(v, "100e6");
    ASSERT_TRUE(conn::split_kv("settings:bias_tx=true,foo=1", k, v));
    EXPECT_EQ(v, "bias_tx=true,foo=1");
    EXPECT_FALSE(conn::split_kv("no-colon", k, v));
    EXPECT_FALSE(conn::split_kv(":empty-key", k, v));
    EXPECT_TRUE(conn::truthy("TRUE"));
    EXPECT_TRUE(conn::truthy("1"));
    EXPECT_FALSE(conn::truthy("None"));
}

TEST(ConnectorSockets, ProbeConnectIsHarmlessAndIqIsFloat32) {
    conn::IqServer iq(0);
    ASSERT_GT(iq.port(), 0);

    // OWRX decides the source is up by connecting and closing immediately.
    const int probe = connect_loopback(iq.port());
    ASSERT_GE(probe, 0);
    ::close(probe);

    const int fd = connect_loopback(iq.port());
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(wait_for([&] { return iq.clients() >= 1; }));

    std::vector<std::complex<float>> sent(1024);
    for (size_t i = 0; i < sent.size(); ++i) sent[i] = {static_cast<float>(i) / 1024.0f, -1.0f};
    std::vector<std::complex<float>> got(sent.size());
    ASSERT_TRUE(drain(fd, got.data(), sizeof(std::complex<float>) * got.size(), iq, sent.data(), sent.size()));

    // The stream is a repeat of one block, so any offset into it matches.
    const auto* first = got.data();
    size_t phase = sent.size();
    for (size_t i = 0; i < sent.size(); ++i) if (sent[i] == first[0]) { phase = i; break; }
    ASSERT_LT(phase, sent.size());
    for (size_t i = 0; i < 64; ++i) EXPECT_EQ(got[i], sent[(phase + i) % sent.size()]);

    ASSERT_TRUE(wait_for([&] { return iq.clients() == 1; })) << "the probe connection was never reaped";
    ::close(fd);
    ASSERT_TRUE(wait_for([&] { return iq.clients() == 0; }));
}

TEST(ConnectorSockets, SlowReaderIsDroppedNotBlocking) {
    conn::IqServer iq(0);
    const int fd = connect_loopback(iq.port());
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(wait_for([&] { return iq.clients() == 1; }));

    std::vector<std::complex<float>> block(conn::IqServer::RING_SAMPLES);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 8; ++i) iq.push(block.data(), block.size());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000);
    EXPECT_GT(iq.dropped(), 0u);
    ::close(fd);
}

TEST(ConnectorSockets, ControlLinesReachTheApp) {
    conn::ControlServer ctl(0);
    ASSERT_GT(ctl.port(), 0);
    const int fd = connect_loopback(ctl.port());
    ASSERT_GE(fd, 0);

    const std::string msg = "center_freq:105700000\nrf_gain:LNA=24\nbogus\n";
    ASSERT_EQ(::send(fd, msg.data(), msg.size(), 0), static_cast<ssize_t>(msg.size()));

    std::vector<std::pair<std::string, std::string>> got;
    ASSERT_TRUE(wait_for([&] {
        std::string k, v;
        while (ctl.pop(k, v)) got.emplace_back(k, v);
        return got.size() >= 2;
    }));
    EXPECT_EQ(got[0].first, "center_freq");
    EXPECT_EQ(got[0].second, "105700000");
    EXPECT_EQ(got[1].first, "rf_gain");
    EXPECT_EQ(got[1].second, "LNA=24");
    EXPECT_EQ(got.size(), 2u);
    ::close(fd);
}

TEST(ConnectorSink, IqSwapExchangesTheComponents) {
    conn::IqServer iq(0);
    conn::IqSinkBlock sink("sink", iq, 1024);
    const int fd = connect_loopback(iq.port());
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(wait_for([&] { return iq.clients() == 1; }));

    sink.iqswap.store(true);
    const std::complex<float> in[2] = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    std::complex<float> out[2];
    size_t have = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (have < sizeof out && std::chrono::steady_clock::now() < deadline) {
        sink.in.writeN(in, 2);
        EXPECT_TRUE(sink.procedure().is_ok());
        const ssize_t n = ::recv(fd, reinterpret_cast<char*>(out) + have, sizeof out - have, 0);
        if (n > 0) have += static_cast<size_t>(n);
    }
    ASSERT_EQ(have, sizeof out);
    const bool aligned = out[0].real() == 2.0f;
    EXPECT_FLOAT_EQ(out[aligned ? 0 : 1].real(), 2.0f);
    EXPECT_FLOAT_EQ(out[aligned ? 0 : 1].imag(), 1.0f);
    EXPECT_TRUE(sink.procedure().is_err());
    ::close(fd);
}
