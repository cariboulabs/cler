#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "cler.hpp"
#include "desktop_blocks/fm/rds.hpp"
#include "desktop_blocks/fm/fm_mpx_decoder.hpp"

namespace {

constexpr double kMpxRate = 240e3;
constexpr double kPilotHz = 19e3;
constexpr double kRdsBaud = 1187.5;

// Group 0A carries two PS chars per group, 2A four RT chars per group.
std::vector<uint32_t> make_groups(uint16_t pi, const std::string& ps, const std::string& rt) {
    std::vector<uint32_t> blocks;
    auto push = [&](uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
        blocks.push_back(rds::encode_block(a, rds::Decoder::OFFSET_A));
        blocks.push_back(rds::encode_block(b, rds::Decoder::OFFSET_B));
        blocks.push_back(rds::encode_block(c, rds::Decoder::OFFSET_C));
        blocks.push_back(rds::encode_block(d, rds::Decoder::OFFSET_D));
    };
    const uint16_t pty = 10;
    for (int rep = 0; rep < 3; ++rep) {
        for (uint16_t seg = 0; seg < 4; ++seg) {
            uint16_t b = (0u << 12) | (0u << 11) | (1u << 10) | (pty << 5) | seg;
            uint16_t d = static_cast<uint16_t>((uint8_t)ps[2 * seg] << 8 | (uint8_t)ps[2 * seg + 1]);
            push(pi, b, 0xE0CD, d);
        }
        for (uint16_t seg = 0; seg < 16; ++seg) {
            if (4 * seg >= rt.size()) break;
            auto ch = [&](size_t i) -> uint16_t { return i < rt.size() ? (uint8_t)rt[i] : 0x0D; };
            uint16_t b = (2u << 12) | (1u << 10) | (pty << 5) | seg;
            push(pi, b, static_cast<uint16_t>(ch(4 * seg) << 8 | ch(4 * seg + 1)),
                 static_cast<uint16_t>(ch(4 * seg + 2) << 8 | ch(4 * seg + 3)));
        }
    }
    return blocks;
}

std::vector<bool> to_bits(const std::vector<uint32_t>& blocks) {
    std::vector<bool> bits;
    for (uint32_t blk : blocks)
        for (int i = 25; i >= 0; --i) bits.push_back((blk >> i) & 1u);
    return bits;
}

struct Mpx {
    std::vector<float> samples;
    double pilot_amp = 0.09;
};

// L/R tones, pilot, 38 kHz DSB-SC stereo difference, optional RDS at 57 kHz
// (differential + biphase, rectangular half-bits), all phase-locked to the pilot.
Mpx synth(double seconds, double left_hz, double right_hz, const std::vector<bool>* rds_bits) {
    Mpx m;
    const size_t n = static_cast<size_t>(seconds * kMpxRate);
    m.samples.resize(n);
    const double half_bit = kMpxRate / (2.0 * kRdsBaud);
    bool prev = false;
    for (size_t i = 0; i < n; ++i) {
        const double t = i / kMpxRate;
        const double l = left_hz > 0 ? 0.5 * std::sin(2 * M_PI * left_hz * t) : 0.0;
        const double r = right_hz > 0 ? 0.5 * std::sin(2 * M_PI * right_hz * t) : 0.0;
        const double wp = 2 * M_PI * kPilotHz * t;
        double s = 0.45 * (l + r) + m.pilot_amp * std::sin(wp) + 0.45 * (l - r) * std::sin(2 * wp);
        if (rds_bits && !rds_bits->empty()) {
            const size_t half_idx = static_cast<size_t>(i / half_bit);
            const size_t bit_idx = (half_idx / 2) % rds_bits->size();
            if (half_idx % 2 == 0 && static_cast<size_t>(i / half_bit) != static_cast<size_t>((i - 1) / half_bit)) {
                prev = prev ^ (*rds_bits)[bit_idx];
            }
            const double level = (prev ? 1.0 : -1.0) * ((half_idx % 2 == 0) ? 1.0 : -1.0);
            s += 0.03 * level * std::sin(3 * wp);
        }
        m.samples[i] = static_cast<float>(s);
    }
    return m;
}

std::vector<float> run(FMMpxDecoderBlock& dec, const std::vector<float>& mpx) {
    cler::Channel<float> out(1 << 16);
    std::vector<float> audio;
    size_t pos = 0;
    while (pos < mpx.size()) {
        auto [wptr, wsize] = dec.in.write_dbf();
        size_t n = std::min(wsize, mpx.size() - pos);
        for (size_t i = 0; i < n; ++i) wptr[i] = mpx[pos + i];
        dec.in.commit_write(n);
        pos += n;
        while (dec.procedure(&out).is_ok()) {
            auto [rptr, rsize] = out.read_dbf();
            audio.insert(audio.end(), rptr, rptr + rsize);
            out.commit_read(rsize);
        }
    }
    return audio;
}

double power(const std::vector<float>& a, size_t from, size_t stride, size_t offset) {
    double p = 0; size_t c = 0;
    for (size_t i = from + offset; i < a.size(); i += stride) { p += a[i] * a[i]; ++c; }
    return c ? p / c : 0;
}

}  // namespace

TEST(RdsBits, EncodeDecodeRoundTrip) {
    const std::string ps = "CLER FM!";
    const std::string rt = "HELLO FROM CLER RADIO";
    auto bits = to_bits(make_groups(0x1234, ps, rt));
    rds::Decoder dec;
    // leading junk then the stream
    for (int i = 0; i < 37; ++i) dec.push_bit(i % 3 == 0);
    for (bool b : bits) dec.push_bit(b);
    const auto& st = dec.station();
    EXPECT_TRUE(st.synced);
    EXPECT_EQ(st.pi, 0x1234);
    EXPECT_EQ(st.pty, 10);
    EXPECT_TRUE(st.tp);
    EXPECT_STREQ(st.ps, ps.c_str());
    EXPECT_STREQ(st.rt, rt.c_str());
    EXPECT_GT(st.groups_ok, 20u);
    EXPECT_EQ(st.blocks_bad, 0u);
}

TEST(RdsBits, CorruptBlockBDoesNotMisplaceCharacters) {
    // one clean pass of PS, then the same groups with every B block damaged
    // and different characters: nothing may change
    auto clean = make_groups(0x1234, "CLER FM!", "");
    // damage the decoder cannot correct: scattered bits whose syndrome is not
    // in the burst table (checked, so the test does not depend on the table)
    uint32_t damage = 0;
    for (uint32_t cand : {0x2000004u, 0x1000010u, 0x0800020u, 0x0400001u}) {
        if (rds::Decoder::correct(rds::encode_block(0, rds::Decoder::OFFSET_B) ^ cand, rds::Decoder::OFFSET_B) == 0) { damage = cand; break; }
    }
    ASSERT_NE(damage, 0u);
    for (int damaged : {1, 0, 2}) {  // B only; A and B; C and D
        auto dirty = make_groups(0x1234, "XXXXXXXX", "");
        for (size_t i = 0; i < dirty.size(); ++i) {
            const size_t k = i % 4;
            if (k == static_cast<size_t>(damaged) || (damaged == 0 && k == 1) || (damaged == 2 && k == 3)) dirty[i] ^= damage;
        }
        rds::Decoder dec;
        for (bool b : to_bits(clean)) dec.push_bit(b);
        ASSERT_STREQ(dec.station().ps, "CLER FM!");
        const uint32_t groups_before = dec.station().groups_ok;
        for (bool b : to_bits(dirty)) dec.push_bit(b);
        EXPECT_STREQ(dec.station().ps, "CLER FM!") << "damaged " << damaged;
        EXPECT_EQ(dec.station().groups_ok, groups_before);
        EXPECT_GT(dec.station().blocks_bad, 0u);
    }
}

TEST(RdsBits, SyndromeOfValidBlockIsOffset) {
    EXPECT_EQ(rds::Decoder::syndrome(rds::encode_block(0xBEEF, rds::Decoder::OFFSET_B)), rds::Decoder::OFFSET_B);
    EXPECT_NE(rds::Decoder::syndrome(rds::encode_block(0xBEEF, rds::Decoder::OFFSET_B) ^ 0x4000), rds::Decoder::OFFSET_B);
}

TEST(FmMpxDecoder, StereoSeparationAndPilotLock) {
    FMMpxDecoderBlock dec("mpx", kMpxRate, 5, 0.0);
    auto mpx = synth(2.0, 1000.0, 0.0, nullptr);
    auto audio = run(dec, mpx.samples);
    ASSERT_GT(audio.size(), 48000u * 3u);
    EXPECT_TRUE(dec.stereo_locked());
    EXPECT_GT(dec.pilot_snr_db(), 25.0f);
    EXPECT_NEAR(dec.pilot_level(), 0.09f, 0.02f);
    const size_t skip = 48000 * 2;  // one second of settling, interleaved
    const double pl = power(audio, skip, 2, 0), pr = power(audio, skip, 2, 1);
    const double sep_db = 10 * std::log10(pl / (pr + 1e-12));
    EXPECT_GT(sep_db, 20.0) << "L " << pl << " R " << pr;
    // L = 0.5*(sum+diff) = 0.45 * tone(amp 0.5)
    EXPECT_NEAR(10 * std::log10(pl), 10 * std::log10(0.225 * 0.225 / 2), 1.5);

    dec.set_stereo(false);
    auto mono = run(dec, mpx.samples);
    const double ml = power(mono, skip, 2, 0), mr = power(mono, skip, 2, 1);
    EXPECT_NEAR(ml, mr, 0.05 * ml);
}

TEST(FmMpxDecoder, RdsOverMpx) {
    const std::string ps = "CLER FM!";
    const std::string rt = "HELLO FROM CLER RADIO";
    auto bits = to_bits(make_groups(0x4A55, ps, rt));
    FMMpxDecoderBlock dec("mpx", kMpxRate, 5, 50.0);
    auto mpx = synth(6.0, 440.0, 880.0, &bits);
    run(dec, mpx.samples);
    auto st = dec.rds_station();
    EXPECT_NEAR(static_cast<double>(dec.rds_halfbits()), 6.0 * 2375.0, 30.0);
    EXPECT_TRUE(st.synced);
    EXPECT_EQ(st.pi, 0x4A55);
    EXPECT_STREQ(st.ps, ps.c_str());
    EXPECT_STREQ(st.rt, rt.c_str());
    EXPECT_GT(st.groups_ok, 10u);
    EXPECT_LT(st.blocks_bad, st.blocks_total / 10 + 1);
}

TEST(RdsBits, ShortBurstErrorsAreCorrected) {
    const uint32_t block = rds::encode_block(0xBEEF, rds::Decoder::OFFSET_B);
    for (int len = 1; len <= rds::Decoder::MAX_BURST; ++len) {
        for (int pos = 0; pos + len <= 26; ++pos) {
            const uint32_t burst = ((1u << len) - 1u) << pos;  // all-ones burst
            EXPECT_EQ(rds::Decoder::correct(block ^ burst, rds::Decoder::OFFSET_B), block) << "len " << len << " pos " << pos;
            const uint32_t ends = ((1u << (len - 1)) | 1u) << pos;  // only the ends set
            EXPECT_EQ(rds::Decoder::correct(block ^ ends, rds::Decoder::OFFSET_B), block) << "ends len " << len << " pos " << pos;
        }
    }
    // a stream where every B block carries a single flipped bit still decodes
    auto groups = make_groups(0x1234, "CLER FM!", "HELLO");
    for (size_t i = 1; i < groups.size(); i += 4) groups[i] ^= 1u << (i % 26);
    rds::Decoder dec;
    for (bool b : to_bits(groups)) dec.push_bit(b);
    EXPECT_STREQ(dec.station().ps, "CLER FM!");
    EXPECT_GT(dec.station().blocks_corrected, 10u);
    EXPECT_EQ(dec.station().blocks_bad, 0u);
}
