#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

#include "cler.hpp"
#include "desktop_blocks/bpsk/bpsk_common.hpp"
#include "desktop_blocks/bpsk/deframer.hpp"
#include "desktop_blocks/bpsk/framer.hpp"
#include "desktop_blocks/bpsk/modulator.hpp"
#include "desktop_blocks/bpsk/receiver.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/math/gain.hpp"
#include "desktop_blocks/noise/awgn.hpp"
#include "desktop_blocks/noise/clock_drift.hpp"

using cf = std::complex<float>;
using Message = std::vector<uint8_t>;

namespace {

constexpr double FS = 2e6;
constexpr unsigned int SPS = 8;
constexpr size_t BUF = 16384;

std::vector<Message> random_messages(size_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> len(1, static_cast<int>(bpsk::MAX_PAYLOAD));
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<Message> msgs(count);
    for (auto& m : msgs) {
        m.resize(static_cast<size_t>(len(rng)));
        for (auto& b : m) b = static_cast<uint8_t>(byte(rng));
    }
    return msgs;
}

void push_records(cler::Channel<uint8_t>& ch, const std::vector<Message>& msgs) {
    for (const auto& m : msgs) {
        const uint8_t len = static_cast<uint8_t>(m.size());
        ASSERT_EQ(ch.writeN(&len, 1), 1u);
        ASSERT_EQ(ch.writeN(m.data(), m.size()), m.size());
    }
}

// Splits length-prefixed records drained from a channel.
void pop_records(cler::Channel<uint8_t>& ch, std::vector<uint8_t>& carry, std::vector<Message>& out) {
    uint8_t buf[1024];
    size_t n;
    while ((n = ch.readN(buf, sizeof(buf))) > 0) carry.insert(carry.end(), buf, buf + n);
    size_t pos = 0;
    while (pos < carry.size() && pos + 1 + carry[pos] <= carry.size()) {
        out.emplace_back(carry.begin() + static_cast<long>(pos) + 1,
                         carry.begin() + static_cast<long>(pos) + 1 + carry[pos]);
        pos += 1 + carry[pos];
    }
    carry.erase(carry.begin(), carry.begin() + static_cast<long>(pos));
}

size_t count_matches(const std::vector<Message>& sent, const std::vector<Message>& got) {
    // Frames arrive in order; a lost frame is skipped, never reordered.
    size_t matches = 0, j = 0;
    for (const auto& m : got) {
        while (j < sent.size() && sent[j] != m) ++j;
        if (j < sent.size()) { ++matches; ++j; }
    }
    return matches;
}

struct LinkParams {
    double ppm = 0.0;
    double cfo_hz = 0.0;
    double esn0_db = 20.0;
    bool invert = false;
    uint32_t seed = 7;
};

struct LinkResult {
    std::vector<Message> sent;
    std::vector<Message> got;
    uint64_t crc_failures = 0;
    uint64_t inverted = 0;
    double cfo_hz = 0.0;
    float rate_ppm = 0.0f;
    bool locked = false;
};

// framer -> modulator -> drift -> CFO -> (pi rotation) -> AWGN -> receiver -> deframer,
// stepped by hand; AWGN last so its Es/N0 calibration is exact.
LinkResult run_link(const LinkParams& p, size_t messages) {
    BPSKFramerBlock framer("framer");
    BPSKModulatorBlock mod("mod", SPS, 0.35f, 10, BUF);
    ClockDriftBlock drift("drift", p.ppm, 0.3, BUF);
    FrequencyShiftBlock shift("shift", p.cfo_hz, FS, BUF);
    GainBlock<cf> rot("rot", cf(p.invert ? -1.0f : 1.0f, 0.0f), BUF);
    NoiseAWGNBlock<cf> awgn("awgn", bpsk::noise_stddev_for_esn0_db(static_cast<float>(p.esn0_db)), BUF, p.seed);
    BPSKReceiverBlock rx("rx", FS);
    BPSKDeframerBlock deframer("deframer");
    cler::Channel<uint8_t> packets(BUF);

    LinkResult r;
    r.sent = random_messages(messages, p.seed);
    push_records(framer.in, r.sent);

    std::vector<uint8_t> carry;
    std::vector<float> tail(256, 0.0f);
    size_t tail_symbols = 0;
    const size_t flush_symbols = 2 * 4096 / SPS + 64;  // two receiver blocks of silence
    for (int iter = 0; iter < 100000; ++iter) {
        framer.procedure(&mod.in);
        if (framer.frames_sent() == messages && framer.in.size() == 0 && tail_symbols < flush_symbols) {
            tail_symbols += mod.in.writeN(tail.data(), std::min(tail.size(), flush_symbols - tail_symbols));
        }
        mod.procedure(&drift.in);
        drift.procedure(&shift.in);
        shift.procedure(&rot.in);
        rot.procedure(&awgn.in);
        awgn.procedure(&rx.in);
        rx.procedure(&deframer.in);
        deframer.procedure(&packets);
        const size_t before = r.got.size();
        pop_records(packets, carry, r.got);
        if (r.got.size() > before && r.got.size() <= messages * 4 / 5) {
            // Loop state while frames are still streaming in; the flush tail is silence.
            r.cfo_hz = rx.cfo_hz() + rx.costas_freq_hz();
            r.rate_ppm = rx.rate_error_ppm();
            r.locked = rx.locked();
        }
        if (tail_symbols == flush_symbols && mod.in.size() == 0 && drift.in.size() == 0 &&
            shift.in.size() == 0 && rot.in.size() == 0 && awgn.in.size() == 0 &&
            rx.in.size() == 0 && deframer.in.size() == 0) {
            break;
        }
    }
    r.crc_failures = deframer.crc_failures();
    r.inverted = deframer.inverted_frames();
    return r;
}

}  // namespace

// 1. Framer straight into deframer: every record comes back byte-identical.
TEST(BPSKBlocks, FramerDeframerRoundTrip) {
    BPSKFramerBlock framer("framer", 0);
    BPSKDeframerBlock deframer("deframer");
    cler::Channel<float> symbols(BUF);
    cler::Channel<uint8_t> packets(BUF);
    const auto sent = random_messages(20, 11);
    push_records(framer.in, sent);

    std::vector<uint8_t> carry;
    std::vector<Message> got;
    std::vector<float> s(BUF);
    std::vector<cf> z(BUF);
    for (int i = 0; i < 1000 && got.size() < sent.size(); ++i) {
        framer.procedure(&symbols);
        const size_t n = symbols.readN(s.data(), std::min(symbols.size(), deframer.in.space()));
        for (size_t k = 0; k < n; ++k) z[k] = cf(s[k], 0.0f);
        deframer.in.writeN(z.data(), n);
        deframer.procedure(&packets);
        pop_records(packets, carry, got);
    }
    ASSERT_EQ(got.size(), sent.size());
    for (size_t i = 0; i < sent.size(); ++i) EXPECT_EQ(got[i], sent[i]) << "message " << i;
    EXPECT_EQ(deframer.crc_failures(), 0u);
    EXPECT_EQ(framer.records_rejected(), 0u);
}

// 2. Unit-energy RRC taps; TX RRC * RX RRC is Nyquist (no ISI at symbol centres);
//    the modulator's impulse response is exactly the taps.
TEST(BPSKBlocks, ModulatorPulseIsUnitEnergyNyquist) {
    const auto h = bpsk::rrc_taps(SPS, 10, 0.35f);
    double e = 0.0;
    for (float v : h) e += static_cast<double>(v) * v;
    EXPECT_NEAR(e, 1.0, 1e-6);

    const long n = static_cast<long>(h.size());
    const long centre = n - 1;
    for (long k = -9; k <= 9; ++k) {
        const long lag = centre + k * static_cast<long>(SPS);
        double acc = 0.0;
        for (long i = 0; i < n; ++i) {
            const long j = lag - i;
            if (j >= 0 && j < n) acc += static_cast<double>(h[static_cast<size_t>(i)]) * h[static_cast<size_t>(j)];
        }
        if (k == 0) EXPECT_NEAR(acc, 1.0, 1e-3);
        else EXPECT_LT(std::fabs(acc), 1e-2) << "ISI at symbol offset " << k;
    }

    BPSKModulatorBlock mod("mod", SPS, 0.35f, 10, BUF);
    cler::Channel<cf> out(BUF);
    std::vector<float> impulse(12, 0.0f);
    impulse[0] = 1.0f;
    mod.in.writeN(impulse.data(), impulse.size());
    ASSERT_TRUE(mod.procedure(&out).is_ok());
    std::vector<cf> y(out.size());
    out.readN(y.data(), y.size());
    ASSERT_GE(y.size(), h.size());
    for (size_t i = 0; i < h.size(); ++i) {
        EXPECT_NEAR(y[i].real(), h[i], 1e-6f);
        EXPECT_EQ(y[i].imag(), 0.0f);
    }
}

// 3. Clean link.
TEST(BPSKBlocks, EndToEndClean) {
    const auto r = run_link({0.0, 0.0, 20.0, false, 7}, 50);
    EXPECT_EQ(count_matches(r.sent, r.got), 50u);
    EXPECT_EQ(r.got.size(), 50u);
    EXPECT_EQ(r.crc_failures, 0u);
    EXPECT_TRUE(r.locked);
}

// 4. No shared clock: HackRF + Pluto worst case, 45 ppm at 918.5 MHz ~ 41 kHz.
TEST(BPSKBlocks, EndToEndNoSharedClock) {
    for (double sign : {1.0, -1.0}) {
        const auto r = run_link({sign * 50.0, sign * 41e3, 15.0, false, 21}, 50);
        const size_t ok = count_matches(r.sent, r.got);
        EXPECT_GE(ok, 48u) << "ppm " << sign * 50 << " CFO " << sign * 41e3;
        EXPECT_NEAR(r.cfo_hz, sign * 41e3, 1e3);
        EXPECT_NEAR(r.rate_ppm, sign * 50.0, 30.0);
        std::printf("    ppm %+.0f CFO %+.0f Hz: %zu/50 ok, %llu CRC fail, est CFO %.0f Hz, est rate %.1f ppm\n",
            sign * 50, sign * 41e3, ok, static_cast<unsigned long long>(r.crc_failures), r.cfo_hz, r.rate_ppm);
    }
}

// 5. A pi rotation in the channel is undone by the preamble sign.
TEST(BPSKBlocks, PreambleResolvesPhaseAmbiguity) {
    const auto r = run_link({0.0, 0.0, 20.0, true, 9}, 20);
    EXPECT_EQ(count_matches(r.sent, r.got), 20u);
    EXPECT_EQ(r.crc_failures, 0u);
    EXPECT_EQ(r.inverted, 20u) << "frames should arrive with a negative preamble peak";
}

// 6. Coarse CFO on its own: a tone and a noisy BPSK signal, |offset| up to 100 kHz.
TEST(BPSKBlocks, CoarseCfoEstimate) {
    bpsk::CoarseCfoEstimator est(4096);
    std::vector<cf> x(4096);
    for (double f : {10e3, -10e3, 41e3, -41e3, 100e3, -100e3}) {
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = std::polar(1.0f, static_cast<float>(2.0 * M_PI * std::fmod(f / FS * static_cast<double>(i), 1.0)));
        }
        EXPECT_NEAR(est.estimate(x.data()) * FS, f, 1e3) << "tone " << f;

        BPSKModulatorBlock mod("mod", SPS, 0.35f, 10, BUF);
        FrequencyShiftBlock shift("shift", f, FS, BUF);
        NoiseAWGNBlock<cf> awgn("awgn", bpsk::noise_stddev_for_esn0_db(15.0f), BUF, 5);
        cler::Channel<cf> out(BUF);
        std::mt19937 rng(3);
        std::vector<float> syms(600);
        for (auto& s : syms) s = (rng() & 1u) ? 1.0f : -1.0f;
        mod.in.writeN(syms.data(), syms.size());
        while (out.size() < x.size()) {
            mod.procedure(&shift.in);
            shift.procedure(&awgn.in);
            awgn.procedure(&out);
        }
        out.readN(x.data(), x.size());
        EXPECT_NEAR(est.estimate(x.data()) * FS, f, 1e3) << "BPSK " << f;
    }
}

// Loop gains follow the Rice formula (worked example in the design notes).
TEST(BPSKBlocks, RiceLoopGains) {
    const auto g = bpsk::pi_gains(0.01f, 0.707f, 2.0f, 1.0f);
    EXPECT_NEAR(g.k1, 0.0132f, 1e-4f);
    EXPECT_NEAR(g.k2, 1.75e-4f, 1e-6f);
}
