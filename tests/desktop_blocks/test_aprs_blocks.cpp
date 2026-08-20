#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "cler.hpp"
#include "desktop_blocks/aprs/aprs.hpp"
#include "desktop_blocks/aprs/afsk_demod.hpp"

namespace {

// Bell 202 audio for an NRZI bit stream, with a tone offset, an amplitude
// scale and noise, padded with silence either side.
std::vector<float> tones(const bool* tx, size_t nb, double fs, double offset_hz, float amp, float noise) {
    std::vector<float> out;
    const size_t sps = static_cast<size_t>(std::lround(fs / 1200.0));
    std::srand(4242);
    auto n = [&]() { return noise * (std::rand() / static_cast<float>(RAND_MAX) - 0.5f); };
    for (size_t i = 0; i < sps * 20; ++i) out.push_back(n());
    double ph = 0.0;
    for (size_t i = 0; i < nb; ++i) {
        const double f = (tx[i] ? 1200.0 : 2200.0) + offset_hz;
        for (size_t k = 0; k < sps; ++k) {
            out.push_back(amp * static_cast<float>(std::sin(ph)) + n());
            ph += 2.0 * M_PI * f / fs;
        }
    }
    for (size_t i = 0; i < sps * 20; ++i) out.push_back(n());
    return out;
}

std::vector<aprs::Packet> run(AFSKDemodBlock& d, const std::vector<float>& audio) {
    cler::Channel<aprs::Packet> out(256);
    std::vector<aprs::Packet> got;
    size_t pos = 0;
    while (pos < audio.size()) {
        auto [w, ws] = d.in.write_dbf();
        const size_t n = std::min(ws, audio.size() - pos);
        for (size_t i = 0; i < n; ++i) w[i] = audio[pos + i];
        d.in.commit_write(n);
        pos += n;
        while (d.procedure(&out).is_ok()) {
            auto [r, rs] = out.read_dbf();
            got.insert(got.end(), r, r + rs);
            out.commit_read(rs);
        }
    }
    return got;
}

}  // namespace

TEST(AprsBits, AddressRoundTrip) {
    uint8_t a[7];
    char call[10];
    bool last = false, repeated = false;
    for (const auto& [text, name, ssid] : std::vector<std::tuple<const char*, const char*, int>>{
             {"W1AW", "W1AW", 0}, {"4X1RF-9", "4X1RF", 9}, {"WIDE2-15", "WIDE2", 15}, {"APCLER", "APCLER", 0}}) {
        aprs::encode_address(name, ssid, true, a);
        ASSERT_TRUE(aprs::decode_address(a, call, &last, &repeated));
        EXPECT_STREQ(call, text);
        EXPECT_TRUE(last);
        EXPECT_FALSE(repeated);
    }
    // the has-been-repeated bit of a digipeater address
    aprs::encode_address("WIDE1", 1, false, a);
    a[6] |= 0x80;
    ASSERT_TRUE(aprs::decode_address(a, call, &last, &repeated));
    EXPECT_STREQ(call, "WIDE1-1");
    EXPECT_FALSE(last);
    EXPECT_TRUE(repeated);
}

// A byte-exact AX.25 UI frame written out by hand: W1AW>APRS,WIDE2-1 with a
// position report. Addresses are shifted left one bit, the last one has the
// extension bit, then control 0x03 and PID 0xF0.
TEST(AprsBits, ParsesHandBuiltUiFrame) {
    const uint8_t frame[] = {
        // "APRS  " dest, SSID 0
        'A' << 1, 'P' << 1, 'R' << 1, 'S' << 1, ' ' << 1, ' ' << 1, 0x60,
        // "W1AW  " source, SSID 0
        'W' << 1, '1' << 1, 'A' << 1, 'W' << 1, ' ' << 1, ' ' << 1, 0x60,
        // "WIDE2 " digipeater, SSID 1, end of address
        'W' << 1, 'I' << 1, 'D' << 1, 'E' << 1, '2' << 1, ' ' << 1, 0x63,
        0x03, 0xF0,
        '!', '4', '1', '4', '3', '.', '4', '9', 'N', '/', '0', '7', '2', '4', '6', '.', '2', '2', 'W',
        '-', 'A', 'R', 'R', 'L', ' ', 'H', 'Q', '/', 'A', '=', '0', '0', '0', '1', '2', '3',
    };
    aprs::Packet p;
    ASSERT_TRUE(aprs::parse(frame, sizeof(frame), p));
    EXPECT_STREQ(p.source, "W1AW");
    EXPECT_STREQ(p.dest, "APRS");
    EXPECT_STREQ(p.path, "WIDE2-1");
    EXPECT_EQ(p.type, '!');
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 41 + 43.49 / 60.0, 1e-6);
    EXPECT_NEAR(p.lon, -(72 + 46.22 / 60.0), 1e-6);
    EXPECT_EQ(p.symbol_table, '/');
    EXPECT_EQ(p.symbol_code, '-');
    EXPECT_STREQ(p.comment, "ARRL HQ/A=000123");
    EXPECT_TRUE(p.has_altitude);
    EXPECT_EQ(p.altitude_ft, 123);
}

TEST(AprsBits, RejectsNonUiAndShortFrames) {
    uint8_t frame[32] = {};
    aprs::encode_address("APRS", 0, false, frame);
    aprs::encode_address("W1AW", 0, true, frame + 7);
    frame[14] = 0x03;
    frame[15] = 0xF0;
    aprs::Packet p;
    EXPECT_TRUE(aprs::parse(frame, 16, p));
    frame[14] = 0x13;                      // not a UI frame
    EXPECT_FALSE(aprs::parse(frame, 16, p));
    frame[14] = 0x03;
    frame[15] = 0xCF;                      // not no-layer-3
    EXPECT_FALSE(aprs::parse(frame, 16, p));
    EXPECT_FALSE(aprs::parse(frame, 15, p));
}

// A Mic-E frame built byte by byte from the spec's encoding tables, not from
// this file's encoder: destination "S32U6T" carries latitude 33 25.64 N (digit
// + message bit per character), N/S in character 4, longitude offset 0 in
// character 5 and W/E in character 6.
TEST(AprsBits, MicEDestinationHandBuilt) {
    uint8_t frame[64] = {};
    aprs::encode_address("S32U6T", 0, false, frame);
    aprs::encode_address("W1AW", 7, true, frame + 7);
    frame[14] = 0x03;
    frame[15] = 0xF0;
    // "S32U6T" byte 5 is '6', so the longitude offset is 0 and the degrees
    // octet is plain: d = byte - 28. Longitude 72 07.44 W.
    const uint8_t lon_deg = 72 + 28;
    const uint8_t lon_min = 7 + 88;          // minutes < 10 use the +60 form
    const uint8_t lon_hun = 44 + 28;
    // speed 20 kn, course 251 deg: SP=2, DC=0*10+2=2, SE=51
    const uint8_t sp = 2 + 28 + 80, dc = 2 + 28, se = 51 + 28;
    const uint8_t info[] = {'`', lon_deg, lon_min, lon_hun, sp, dc, se, '>', '/'};
    std::memcpy(frame + 16, info, sizeof(info));

    aprs::Packet p;
    ASSERT_TRUE(aprs::parse(frame, 16 + sizeof(info), p));
    EXPECT_STREQ(p.source, "W1AW-7");
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 33 + 25.64 / 60.0, 1e-6);
    EXPECT_NEAR(p.lon, -(72 + 7.44 / 60.0), 1e-6);
    EXPECT_NEAR(p.speed, 20.0f, 1e-3);
    EXPECT_NEAR(p.course, 251.0f, 1e-3);
    EXPECT_EQ(p.symbol_code, '>');
    EXPECT_EQ(p.symbol_table, '/');
}

// The two longitude-degree special cases the spec calls out, which the encoder
// below never emits (its search finds the plain form first): with the offset
// bit set, byte-28+100 landing in 180-189 means subtract 80, and in 190-199
// means subtract 190. Both are legal on air; a receiver has to handle them.
TEST(AprsBits, MicELongitudeOffsetSpecialCases) {
    auto build = [](const char* dest6, uint8_t lon_deg, uint8_t lon_min, uint8_t lon_hun,
                    uint8_t* frame) {
        aprs::encode_address(dest6, 0, false, frame);
        aprs::encode_address("W1AW", 0, true, frame + 7);
        frame[14] = 0x03;
        frame[15] = 0xF0;
        // speed 20 kn, course 251 deg, car symbol
        const uint8_t info[] = {'`', lon_deg, lon_min, lon_hun,
                                static_cast<uint8_t>(2 + 28 + 80), static_cast<uint8_t>(2 + 28),
                                static_cast<uint8_t>(51 + 28), '>', '/'};
        std::memcpy(frame + 16, info, sizeof(info));
        return 16 + sizeof(info);
    };
    uint8_t frame[64] = {};
    aprs::Packet p;

    // "S32UVT": character 5 is 'V' (digit 6, bit 1) so the offset is +100, and
    // 'l' - 28 + 100 = 180, in the 180-189 window -> 100 degrees. West.
    size_t n = build("S32UVT", 'l', 7 + 88, 44 + 28, frame);
    ASSERT_TRUE(aprs::parse(frame, n, p));
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 33 + 25.64 / 60.0, 1e-6);
    EXPECT_NEAR(p.lon, -(100 + 7.44 / 60.0), 1e-6);

    // "S32UV4": offset +100 again, character 6 is '4' (bit 0) so east, and
    // '{' - 28 + 100 = 195, in the 190-199 window -> 5 degrees.
    n = build("S32UV4", '{', 30 + 28, 0 + 28, frame);
    ASSERT_TRUE(aprs::parse(frame, n, p));
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lon, 5 + 30.0 / 60.0, 1e-6);
}

TEST(AprsBits, MicEEncoderRoundTrip) {
    for (const auto& [lat, lon, spd, crs] : std::vector<std::tuple<double, double, int, int>>{
             {32.8206, 35.0104, 35, 120}, {-33.8688, 151.2093, 0, 0},
             {33.4276, -112.1240, 20, 251}, {51.5074, -0.1278, 120, 359}}) {
        char dest[8], info[32];
        aprs::encode_mice(lat, lon, spd, crs, '/', '>', dest, info);
        uint8_t frame[64] = {};
        aprs::encode_address(dest, 0, false, frame);
        aprs::encode_address("4X1RF", 9, true, frame + 7);
        frame[14] = 0x03;
        frame[15] = 0xF0;
        const size_t n = std::strlen(info);
        std::memcpy(frame + 16, info, n);
        aprs::Packet p;
        ASSERT_TRUE(aprs::parse(frame, 16 + n, p)) << dest;
        ASSERT_TRUE(p.has_position) << dest;
        EXPECT_NEAR(p.lat, lat, 0.0002) << dest;
        EXPECT_NEAR(p.lon, lon, 0.0002) << dest;
        EXPECT_NEAR(p.speed, spd, 1e-3);
        EXPECT_NEAR(p.course, crs, 1e-3);
    }
}

TEST(AprsBits, StatusAndTimestampedPosition) {
    uint8_t frame[128] = {};
    aprs::encode_address("APCLER", 0, false, frame);
    aprs::encode_address("4Z5DX", 0, true, frame + 7);
    frame[14] = 0x03;
    frame[15] = 0xF0;
    const char* status = ">Haifa APRS digi";
    std::memcpy(frame + 16, status, std::strlen(status));
    aprs::Packet p;
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(status), p));
    EXPECT_EQ(p.type, '>');
    EXPECT_FALSE(p.has_position);
    EXPECT_STREQ(p.comment, "Haifa APRS digi");

    const char* pos = "@092345z3249.20N/03500.60E>180/035mobile";
    std::memcpy(frame + 16, pos, std::strlen(pos));
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(pos), p));
    EXPECT_EQ(p.type, '@');
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 32 + 49.20 / 60.0, 1e-6);
    EXPECT_NEAR(p.lon, 35 + 0.60 / 60.0, 1e-6);
    EXPECT_NEAR(p.course, 180.0f, 1e-3);
    EXPECT_NEAR(p.speed, 35.0f, 1e-3);
    EXPECT_STREQ(p.comment, "mobile");
}

TEST(AprsBits, CompressedPositionAndMalformedReport) {
    uint8_t frame[128] = {};
    aprs::encode_address("APCLER", 0, false, frame);
    aprs::encode_address("4X1RF", 9, true, frame + 7);
    frame[14] = 0x03;
    frame[15] = 0xF0;
    // base-91: 49.5 N, 72.75 W -> (90-lat)*380926 = "5L!!", (lon+180)*190463 = "<*e8"
    const char* info = "!/5L!!<*e8>  T";
    std::memcpy(frame + 16, info, std::strlen(info));
    aprs::Packet p;
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(info), p));
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 49.5, 1e-5);
    EXPECT_NEAR(p.lon, -72.75, 1e-5);
    EXPECT_EQ(p.symbol_table, '/');
    EXPECT_EQ(p.symbol_code, '>');
    EXPECT_LT(p.speed, 0.0f);

    // the same position with the spec's worked altitude example in the cs
    // field: compression type '1' has NMEA source GGA (bits 4-3 = 10), so
    // "S]" is 1.002^(50*91+60) = 10004 feet, not a course and speed
    const char* gga = "!/5L!!<*e8>S]1";
    std::memcpy(frame + 16, gga, std::strlen(gga));
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(gga), p));
    ASSERT_TRUE(p.has_position);
    EXPECT_NEAR(p.lat, 49.5, 1e-5);
    ASSERT_TRUE(p.has_altitude);
    EXPECT_EQ(p.altitude_ft, 10004);
    EXPECT_LT(p.course, 0.0f);
    EXPECT_LT(p.speed, 0.0f);

    // the same cs with an RMC compression type (bits 4-3 = 11) is course/speed
    const char* rmc = "!/5L!!<*e8>S]9";
    std::memcpy(frame + 16, rmc, std::strlen(rmc));
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(rmc), p));
    EXPECT_FALSE(p.has_altitude);
    EXPECT_NEAR(p.course, 200.0f, 1e-3);

    // out-of-range degrees or minutes are not a position either
    const char* oor = "!9943.49N/18246.22W-";
    std::memcpy(frame + 16, oor, std::strlen(oor));
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(oor), p));
    EXPECT_FALSE(p.has_position);

    // a truncated uncompressed report must not fall through to base-91 and
    // invent a position
    const char* bad = "!*249.20N/0350";
    std::memcpy(frame + 16, bad, std::strlen(bad));
    std::memset(frame + 16 + std::strlen(bad), 0, 16);
    ASSERT_TRUE(aprs::parse(frame, 16 + std::strlen(bad), p));
    EXPECT_FALSE(p.has_position);
}

// The HDLC layer is ais::Deframer; what differs from AIS is the preamble --
// a real TNC keys up with flags, not a 0101 training sequence.
TEST(AprsBits, FrameRoundTripThroughFlagPreamble) {
    uint8_t frame[64];
    const size_t n = aprs::encode_ui("APCLER", "4X1RF-9", "WIDE1-1,WIDE2-1", "!3249.23N/03500.62E>hi", frame, sizeof(frame));
    ASSERT_GT(n, 0u);
    std::array<bool, 2048> tx{};
    const size_t nb = aprs::encode_frame(frame, n, tx.data(), tx.size(), 24);

    aprs::Deframer d;
    bool prev = false;
    int got = 0;
    for (size_t i = 0; i < nb; ++i) { const bool bit = !(tx[i] ^ prev); prev = tx[i]; if (d.push_bit(bit)) ++got; }
    ASSERT_EQ(got, 1);
    ASSERT_EQ(d.length(), n);
    EXPECT_EQ(0, std::memcmp(d.payload(), frame, n));
    EXPECT_EQ(d.frames_bad_crc(), 0u);

    aprs::Packet p;
    ASSERT_TRUE(aprs::parse(d.payload(), d.length(), p));
    EXPECT_STREQ(p.source, "4X1RF-9");
    EXPECT_STREQ(p.path, "WIDE1-1,WIDE2-1");
    EXPECT_TRUE(p.has_position);

    // a flipped bit inside the frame is rejected by the FCS
    aprs::Deframer d2;
    tx[nb - 40] = !tx[nb - 40];
    prev = false;
    got = 0;
    for (size_t i = 0; i < nb; ++i) { const bool bit = !(tx[i] ^ prev); prev = tx[i]; if (d2.push_bit(bit)) ++got; }
    EXPECT_EQ(got, 0);
    EXPECT_EQ(d2.frames_bad_crc(), 1u);
}

// clean, +/-5 Hz tone offset, half amplitude
TEST(AfskDemod, LoopbackAtOffsetsAndAmplitudes) {
    uint8_t frame[64];
    const size_t n = aprs::encode_ui("APCLER", "4X1RF-9", "WIDE1-1", "!3249.23N/03500.62E>test", frame, sizeof(frame));
    ASSERT_GT(n, 0u);
    std::array<bool, 2048> tx{};
    const size_t nb = aprs::encode_frame(frame, n, tx.data(), tx.size());

    for (const auto& [offset, amp, noise] : std::vector<std::tuple<double, float, float>>{
             {0.0, 1.0f, 0.0f}, {5.0, 1.0f, 0.02f}, {-5.0, 1.0f, 0.02f},
             {0.0, 0.5f, 0.02f}, {5.0, 0.5f, 0.05f}}) {
        AFSKDemodBlock dem("afsk", 48e3, 1 << 14);
        const auto got = run(dem, tones(tx.data(), nb, 48e3, offset, amp, noise));
        ASSERT_EQ(got.size(), 1u) << "offset " << offset << " amp " << amp
                                  << " bad crc " << dem.frames_bad_crc();
        EXPECT_STREQ(got[0].source, "4X1RF-9");
        EXPECT_STREQ(got[0].path, "WIDE1-1");
        EXPECT_TRUE(got[0].has_position);
        EXPECT_NEAR(got[0].lat, 32 + 49.23 / 60.0, 1e-6);
        EXPECT_NEAR(got[0].lon, 35 + 0.62 / 60.0, 1e-6);
    }
}

TEST(AfskDemod, DecodesBackToBackBursts) {
    uint8_t f1[64], f2[64];
    const size_t n1 = aprs::encode_ui("APCLER", "4Z5DX", "", ">digi up", f1, sizeof(f1));
    const size_t n2 = aprs::encode_ui("APCLER", "4X4HF-1", "WIDE1-1", "!3246.80N/03501.20E-wx", f2, sizeof(f2));
    std::array<bool, 2048> t1{}, t2{};
    const size_t b1 = aprs::encode_frame(f1, n1, t1.data(), t1.size());
    const size_t b2 = aprs::encode_frame(f2, n2, t2.data(), t2.size());
    auto a = tones(t1.data(), b1, 48e3, 0.0, 0.8f, 0.02f);
    const auto b = tones(t2.data(), b2, 48e3, 3.0, 0.4f, 0.02f);
    a.insert(a.end(), b.begin(), b.end());
    AFSKDemodBlock dem("afsk", 48e3, 1 << 14);
    const auto got = run(dem, a);
    ASSERT_EQ(got.size(), 2u) << "bad crc " << dem.frames_bad_crc();
    EXPECT_STREQ(got[0].source, "4Z5DX");
    EXPECT_STREQ(got[1].source, "4X4HF-1");
    EXPECT_EQ(dem.packets(), 2u);
}

#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_examples/aprs_receiver/aprs_sim_source.hpp"

// the whole receive chain against the simulated stations: 2.4 MS/s NBFM ->
// shift -> 1/50 -> FM demod -> AFSK, 20 simulated seconds
TEST(AprsChain, SimulatedStationsDecode) {
    APRSSimSourceBlock sim("sim", 2.4e6, -250e3, 3e3, 1 << 18);
    FrequencyShiftBlock shift("shift", +250e3, 2.4e6, 1 << 18);
    RationalResamplerBlock<1, 50, 160> decim("decim", 60.0f, 1 << 18);
    FMDemodBlock fm("fm", 48e3, 3e3, 1 << 16);
    AFSKDemodBlock afsk("afsk", 48e3, 1 << 14);
    cler::Channel<std::complex<float>> src(1 << 18), c1(1 << 18), c2(1 << 16);
    cler::Channel<float> aud(1 << 16);
    cler::Channel<aprs::Packet> pkt(256);
    std::map<std::string, aprs::Packet> seen;
    auto pump = [](auto& from, auto& to) {
        auto [a, as] = from.read_dbf();
        auto [b, bs] = to.write_dbf();
        const size_t m = std::min(as, bs);
        for (size_t i = 0; i < m; ++i) b[i] = a[i];
        from.commit_read(m);
        to.commit_write(m);
    };
    const size_t total = static_cast<size_t>(2.4e6 * 20);
    size_t produced = 0;
    while (produced < total) {
        if (sim.procedure(&src).is_ok()) produced = sim.in_samples();
        pump(src, shift.in);
        shift.procedure(&c1);
        pump(c1, decim.in);
        decim.procedure(&c2);
        pump(c2, fm.in);
        fm.procedure(&aud);
        pump(aud, afsk.in);
        afsk.procedure(&pkt);
        auto [r, rs] = pkt.read_dbf();
        for (size_t i = 0; i < rs; ++i) {
            auto& p = seen[r[i].source];
            if (r[i].has_position || !p.has_position) p = r[i];
        }
        pkt.commit_read(rs);
    }
    EXPECT_GT(afsk.frames_ok(), 8u);
    EXPECT_EQ(seen.size(), sim.stations().size());
    for (const auto& st : sim.stations()) {
        ASSERT_TRUE(seen.count(st.callsign)) << st.callsign;
        const auto& p = seen[st.callsign];
        EXPECT_TRUE(p.has_position) << st.callsign;
        EXPECT_NEAR(p.lat, st.lat, 0.02) << st.callsign;
        EXPECT_NEAR(p.lon, st.lon, 0.02) << st.callsign;
    }
}
