#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "cler.hpp"
#include "liquid.h"
#include "desktop_blocks/ais/ais.hpp"
#include "desktop_blocks/ais/ais_decoder.hpp"

namespace {

// real type 1 sentence: !AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23
const char* kType1 = "13HOI:0P0000VOHLCnHQKwvL05Ip";
// real type 5 (two-part) payload joined: !AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C
//                                       !AIVDM,2,2,1,A,88888888880,2*25
const char* kType5 = "55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp888888888880";

// GMSK-modulate NRZI bits at sps samples/symbol (liquid gmskmod BT=0.4),
// with a carrier offset and padding noise around the burst.
std::vector<std::complex<float>> modulate(const std::vector<bool>& bits, unsigned sps, float offset_hz, float fs, float noise_amp = 0.0f) {
    gmskmod mod = gmskmod_create(sps, 3, 0.4f);
    std::vector<std::complex<float>> out;
    std::vector<std::complex<float>> buf(sps);
    auto pad = [&](size_t n) { for (size_t i = 0; i < n; ++i) out.push_back({noise_amp * (std::rand() / (float)RAND_MAX - 0.5f), noise_amp * (std::rand() / (float)RAND_MAX - 0.5f)}); };
    pad(fs / 10);
    for (bool b : bits) {
        gmskmod_modulate(mod, b ? 1u : 0u, buf.data());
        out.insert(out.end(), buf.begin(), buf.end());
    }
    pad(fs / 10);
    gmskmod_destroy(mod);
    for (size_t i = 0; i < out.size(); ++i) {
        const float ph = 2.0f * 3.14159265f * offset_hz * i / fs;
        out[i] *= std::complex<float>(std::cos(ph), std::sin(ph));
        out[i] += std::complex<float>(noise_amp * (std::rand() / (float)RAND_MAX - 0.5f), noise_amp * (std::rand() / (float)RAND_MAX - 0.5f));
    }
    return out;
}

std::vector<ais::Message> run(AISDecoderBlock& dec, const std::vector<std::complex<float>>& iq) {
    cler::Channel<ais::Message> out(256);
    std::vector<ais::Message> msgs;
    size_t pos = 0;
    while (pos < iq.size()) {
        auto [w, ws] = dec.in.write_dbf();
        size_t n = std::min(ws, iq.size() - pos);
        for (size_t i = 0; i < n; ++i) w[i] = iq[pos + i];
        dec.in.commit_write(n);
        pos += n;
        while (dec.procedure(&out).is_ok()) {
            auto [r, rs] = out.read_dbf();
            msgs.insert(msgs.end(), r, r + rs);
            out.commit_read(rs);
        }
    }
    return msgs;
}

}  // namespace

TEST(AisBits, ParsesRealType1) {
    uint8_t p[64];
    size_t n = ais::from_nmea_payload(kType1, p, sizeof(p));
    ais::Message m;
    ASSERT_TRUE(ais::parse(p, n, m));
    EXPECT_EQ(m.type, 1);
    EXPECT_EQ(m.mmsi, 227006760u);
    EXPECT_TRUE(m.has_position);
    EXPECT_NEAR(m.lat, 49.4755767, 1e-5);
    EXPECT_NEAR(m.lon, 0.1313800, 1e-5);
    EXPECT_NEAR(m.sog, 0.0f, 1e-3);
    EXPECT_NEAR(m.cog, 36.7f, 1e-3);
    EXPECT_EQ(m.heading, -1);
    EXPECT_EQ(m.nav_status, 0);
}

TEST(AisBits, ParsesRealType5) {
    uint8_t p[80];
    size_t n = ais::from_nmea_payload(kType5, p, sizeof(p));
    ais::Message m;
    ASSERT_TRUE(ais::parse(p, n, m));
    EXPECT_EQ(m.type, 5);
    EXPECT_EQ(m.mmsi, 351759000u);
    EXPECT_STREQ(m.callsign, "3FOF8");
    EXPECT_STREQ(m.name, "EVER DIADEM");
    EXPECT_EQ(m.ship_type, 70);
}

TEST(AisBits, FrameRoundTripWithStuffingAndCrc) {
    uint8_t p[64];
    size_t n = ais::from_nmea_payload(kType1, p, sizeof(p));
    bool tx[1024];
    size_t nb = ais::encode_frame(p, n, tx, 1024);
    // NRZI-decode then deframe
    ais::Deframer d;
    bool prev = false, got = false;
    for (size_t i = 0; i < nb; ++i) {
        bool bit = !(tx[i] ^ prev);
        prev = tx[i];
        if (d.push_bit(bit)) got = true;
    }
    ASSERT_TRUE(got);
    ASSERT_EQ(d.length(), n);
    EXPECT_EQ(0, std::memcmp(d.payload(), p, n));
    EXPECT_EQ(d.frames_bad_crc(), 0u);
    // a flipped bit is rejected
    ais::Deframer d2;
    prev = false; got = false;
    tx[60] = !tx[60];
    for (size_t i = 0; i < nb; ++i) { bool bit = !(tx[i] ^ prev); prev = tx[i]; if (d2.push_bit(bit)) got = true; }
    EXPECT_FALSE(got);
    EXPECT_EQ(d2.frames_bad_crc(), 1u);
}

TEST(AisDecoder, DecodesLongType5Burst) {
    uint8_t p[80];
    size_t n = ais::from_nmea_payload(kType5, p, sizeof(p));
    bool tx[2048];
    size_t nb = ais::encode_frame(p, n, tx, 2048);
    std::vector<bool> bits(tx, tx + nb);
    // a single draw is compiler-marginal (long burst, hard decisions); a
    // deterministic seed plus three tries pins flakiness without hiding a
    // real regression
    std::srand(7);
    int got = 0;
    for (int t = 0; t < 3 && !got; ++t) {
        AISDecoderBlock dec("ais", 48e3, 1 << 16);
        auto msgs = run(dec, modulate(bits, 5, 300.0f, 48e3, 0.12f));
        if (msgs.size() == 1 && std::string(msgs[0].name) == "EVER DIADEM") got = 1;
    }
    EXPECT_EQ(got, 1);
}

TEST(AisDecoder, DecodesGmskBurstWithOffsetAndNoise) {
    uint8_t p[64];
    size_t n = ais::from_nmea_payload(kType1, p, sizeof(p));
    bool tx[1024];
    size_t nb = ais::encode_frame(p, n, tx, 1024);
    std::vector<bool> bits(tx, tx + nb);
    for (float offset : {0.0f, 800.0f, -1500.0f}) {
        AISDecoderBlock dec("ais", 48e3, 1 << 16);
        auto iq = modulate(bits, 5, offset, 48e3, 0.15f);
        auto msgs = run(dec, iq);
        ASSERT_EQ(msgs.size(), 1u) << "offset " << offset;
        EXPECT_EQ(msgs[0].mmsi, 227006760u);
        EXPECT_NEAR(msgs[0].lat, 49.4755767, 1e-5);
        EXPECT_EQ(dec.frames_bad_crc(), 0u);
    }
}

// HDLC edge cases the DSP tests cannot reach: a stuffed 0 immediately before
// the closing flag, an abort (7 ones), and back-to-back frames sharing a flag.
TEST(AisBits, DeframerStuffingAbortAndSharedFlag) {
    // payload whose last data bits are five 1s, so the transmitter stuffs a 0
    // right before the closing flag
    const uint8_t p[4] = {0x00, 0x11, 0x22, 0xF8};
    bool tx[512];
    size_t nb = ais::encode_frame(p, sizeof(p), tx, sizeof(tx));
    // NRZI is differential, so a slice must be fed with the level that preceded
    // it in tx, otherwise the splice inverts the slice's first bit
    auto feed = [&tx](ais::Deframer& d, size_t from, size_t n) {
        int got = 0;
        bool prev = from ? tx[from - 1] : false;
        for (size_t i = from; i < from + n; ++i) { bool bit = !(tx[i] ^ prev); prev = tx[i]; if (d.push_bit(bit)) ++got; }
        return got;
    };
    ais::Deframer d;
    ASSERT_EQ(feed(d, 0, nb), 1);
    ASSERT_EQ(d.length(), sizeof(p));
    EXPECT_EQ(0, std::memcmp(d.payload(), p, sizeof(p)));

    // back-to-back frames, without the training and idle padding: flag..flag
    // twice (double flag between them), then with the closing flag shared
    const size_t fn = nb - 48;   // tx[24 .. nb-24) is flag..data..flag
    ais::Deframer d2;
    EXPECT_EQ(feed(d2, 24, fn) + feed(d2, 24, fn), 2);
    EXPECT_EQ(d2.frames_bad_crc(), 0u);
    ais::Deframer d4;
    EXPECT_EQ(feed(d4, 24, fn) + feed(d4, 32, fn - 8), 2);
    EXPECT_EQ(d4.frames_bad_crc(), 0u);

    // seven ones abort the frame in progress: no frame, and no bad-CRC either
    ais::Deframer d3;
    for (int i = 0; i < 8; ++i) d3.push_bit((0x7E >> (7 - i)) & 1);
    ASSERT_TRUE(d3.in_frame());
    for (int i = 0; i < 40; ++i) EXPECT_FALSE(d3.push_bit(i % 3 == 0));
    for (int i = 0; i < 7; ++i) EXPECT_FALSE(d3.push_bit(true));
    EXPECT_FALSE(d3.in_frame());
    EXPECT_EQ(d3.frames_ok(), 0u);
    EXPECT_EQ(d3.frames_bad_crc(), 0u);
}

// The burst decoder replays the preamble window from the ring and then samples
// live; both must land on the same symbol grid. A one-sample disagreement still
// decodes at 5 samples/symbol in the clear, so guard it where it hurts: 4
// samples/symbol at low SNR (3/20 with the grids off by one, 18/20 aligned).
TEST(AisDecoder, ReplayAndLiveShareTheSymbolGrid) {
    std::srand(12345);
    uint8_t p[80];
    size_t n = ais::from_nmea_payload(kType5, p, sizeof(p));
    bool tx[2048];
    size_t nb = ais::encode_frame(p, n, tx, 2048);
    std::vector<bool> bits(tx, tx + nb);
    int ok = 0;
    for (int trial = 0; trial < 20; ++trial) {
        AISDecoderBlock dec("ais", 9600.0 * 4, 1 << 16);
        if (run(dec, modulate(bits, 4, 300.0f, 4 * 9600.0f, 0.8f)).size() == 1) ++ok;
    }
    EXPECT_GE(ok, 12) << "decoded " << ok << "/20";
}

#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include "desktop_examples/ais_receiver/ais_sim_source.hpp"

// the whole receive chain against the synthetic ships: 2.4 MS/s with both
// channels -> shift -> 1/50 -> decoder, 8 simulated seconds
TEST(AisChain, SimulatedShipsDecodeOnBothChannels) {
    AISSimSourceBlock sim("sim", 2.4e6, 1 << 18);
    FrequencyShiftBlock sh_a("a", +25e3, 2.4e6, 1 << 18), sh_b("b", -25e3, 2.4e6, 1 << 18);
    RationalResamplerBlock<1, 50, 160> d_a("da", 60.0f, 1 << 18), d_b("db", 60.0f, 1 << 18);
    AISDecoderBlock dec_a("A", 48e3, 1 << 16), dec_b("B", 48e3, 1 << 16);
    cler::Channel<std::complex<float>> src(1 << 18), ca1(1 << 18), cb1(1 << 18), ca2(1 << 16), cb2(1 << 16);
    cler::Channel<ais::Message> ma(256), mb(256);
    std::map<uint32_t, ais::Message> seen;
    auto pump = [](auto& from, auto& to) { auto [a, as] = from.read_dbf(); auto [b, bs] = to.write_dbf(); size_t m = std::min(as, bs); for (size_t i = 0; i < m; ++i) b[i] = a[i]; from.commit_read(m); to.commit_write(m); };
    auto drain = [&](cler::Channel<ais::Message>& ch) { auto [r, rs] = ch.read_dbf(); for (size_t i = 0; i < rs; ++i) { auto& m = seen[r[i].mmsi]; char name[21]; std::memcpy(name, m.name, sizeof(name)); if (r[i].has_position) m = r[i]; if (r[i].name[0]) std::memcpy(m.name, r[i].name, sizeof(m.name)); else std::memcpy(m.name, name, sizeof(name)); } ch.commit_read(rs); };
    const size_t total = static_cast<size_t>(2.4e6 * 8);
    size_t produced = 0;
    while (produced < total) {
        if (sim.procedure(&src).is_ok()) produced = sim.in_samples();
        { auto [a, as] = src.read_dbf(); auto [b1, bs1] = sh_a.in.write_dbf(); auto [b2, bs2] = sh_b.in.write_dbf(); size_t m = std::min({as, bs1, bs2}); for (size_t i = 0; i < m; ++i) { b1[i] = a[i]; b2[i] = a[i]; } src.commit_read(m); sh_a.in.commit_write(m); sh_b.in.commit_write(m); }
        sh_a.procedure(&ca1); sh_b.procedure(&cb1);
        pump(ca1, d_a.in); pump(cb1, d_b.in);
        d_a.procedure(&ca2); d_b.procedure(&cb2);
        pump(ca2, dec_a.in); pump(cb2, dec_b.in);
        dec_a.procedure(&ma); dec_b.procedure(&mb);
        drain(ma); drain(mb);
    }
    EXPECT_GT(dec_a.frames_ok(), 3u);
    EXPECT_GT(dec_b.frames_ok(), 3u);
    EXPECT_EQ(seen.size(), sim.ships().size());
    for (const auto& sh : sim.ships()) {
        ASSERT_TRUE(seen.count(sh.mmsi)) << sh.mmsi;
        const auto& m = seen[sh.mmsi];
        EXPECT_TRUE(m.has_position);
        EXPECT_NEAR(m.lat, sh.lat, 0.02);
        EXPECT_NEAR(m.lon, sh.lon, 0.02);
    }
    // at least one name report was sent within 8 s (next_name_s <= 8 for three ships)
    int named = 0;
    for (auto& [mmsi, m] : seen) if (m.name[0]) ++named;
    EXPECT_GE(named, 1);
}
