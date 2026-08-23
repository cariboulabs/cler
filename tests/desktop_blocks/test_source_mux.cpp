#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <complex>
#include "cler.hpp"
#include "desktop_blocks/sources/source_mux.hpp"

namespace {

// drains `src` into a scratch channel for `seconds`, returns samples produced
template <typename Src>
size_t pump(Src& src, double seconds) {
    cler::Channel<std::complex<float>> out(1 << 16);
    size_t total = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        src.procedure(&out);
        auto [p, n] = out.read_dbf();
        out.commit_read(n);
        total += n;
    }
    return total;
}

}  // namespace

TEST(SimSource, PacesToRateAndCarriesTheTone) {
    SimSourceBlock sim("sim", 1e6, 100e6, 100e3, 40.0f);
    const size_t n = pump(sim, 1.0);
    EXPECT_NEAR(static_cast<double>(n), 1e6, 5e4);

    cler::Channel<std::complex<float>> out(1 << 16);
    while (out.size() < 4096) sim.procedure(&out);
    std::complex<float> prev, cur;
    out.pop(prev);
    double phase = 0.0;
    for (int i = 0; i < 2047; ++i) {
        out.pop(cur);
        phase += std::arg(cur * std::conj(prev));
        prev = cur;
    }
    const double f = phase / (2.0 * M_PI) / 2047.0 * 1e6;
    EXPECT_NEAR(f, 100e3, 2e3);
}

TEST(SimSource, SetRateRepaces) {
    SimSourceBlock sim("sim", 1e6);
    sim.set_rate(250e3);
    const size_t n = pump(sim, 0.5);
    EXPECT_NEAR(static_cast<double>(n), 125e3, 1.5e4);
}

TEST(SourceMux, EnumerationAlwaysOffersTheSimulator) {
    auto devs = SourceMux::enumerate();
    ASSERT_FALSE(devs.empty());
    bool sim = false;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::Sim) sim = true;
    EXPECT_TRUE(sim);
    EXPECT_STREQ(SourceMux::kind_name(SourceMux::Kind::Sim), "sim");
}

TEST(SourceMux, EmptyMuxIsIdle) {
    SourceMux mux("mux");
    cler::Channel<std::complex<float>> out(1 << 16);
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
    EXPECT_EQ(mux.procedure(&out).unwrap_err(), cler::Error::NotEnoughSamples);
    EXPECT_TRUE(mux.capabilities().empty());
    EXPECT_DOUBLE_EQ(mux.rate(), 0.0);
}

TEST(SourceMux, SimSelectCapabilitiesSetAndSamples) {
    SourceMux mux("mux");
    mux.select(SourceMux::Kind::Sim, "", 100e6, 2e6);
    EXPECT_EQ(mux.kind(), SourceMux::Kind::Sim);
    EXPECT_DOUBLE_EQ(mux.rate(), 2e6);
    EXPECT_DOUBLE_EQ(mux.center(), 100e6);
    EXPECT_FALSE(mux.lost());

    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 4u);
    EXPECT_EQ(caps[0].id, "freq");
    EXPECT_EQ(caps[0].type, "range");
    EXPECT_DOUBLE_EQ(caps[0].value, 100e6);
    EXPECT_EQ(caps[2].id, "tone_hz");

    mux.set("freq", 101e6);
    mux.set("tone_hz", 250e3);
    mux.set("snr_db", 10.0);
    EXPECT_DOUBLE_EQ(mux.center(), 101e6);
    EXPECT_DOUBLE_EQ(mux.capabilities()[2].value, 250e3);
    EXPECT_FLOAT_EQ(static_cast<float>(mux.capabilities()[3].value), 10.0f);

    const size_t n = pump(mux, 0.3);
    EXPECT_NEAR(static_cast<double>(n), 600e3, 60e3);

    mux.select(SourceMux::Kind::Sim, "", 50e6, 1e6);
    EXPECT_DOUBLE_EQ(mux.rate(), 1e6);
    EXPECT_DOUBLE_EQ(mux.center(), 50e6);
    mux.close();
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
}

#ifdef CLER_HAS_HACKRF
TEST(SourceMux, HackRFIfPresent) {
    auto devs = SourceMux::enumerate();
    const SourceMux::DeviceInfo* hack = nullptr;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::HackRF) hack = &d;
    if (!hack) GTEST_SKIP() << "no HackRF connected";

    SourceMux mux("mux");
    mux.select(SourceMux::Kind::HackRF, hack->id, 100e6, 2.4e6);
    EXPECT_EQ(mux.kind(), SourceMux::Kind::HackRF);
    EXPECT_DOUBLE_EQ(mux.rate(), 2.4e6);
    EXPECT_FALSE(mux.lost());
    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 5u);
    EXPECT_EQ(caps[1].type, "enum");
    EXPECT_EQ(caps[4].id, "amp");
    EXPECT_EQ(caps[4].type, "bool");
    mux.set("lna", 24);
    mux.set("freq", 101.1e6);
    EXPECT_DOUBLE_EQ(mux.capabilities()[2].value, 24.0);
    EXPECT_DOUBLE_EQ(mux.center(), 101.1e6);
    const size_t n = pump(mux, 0.5);
    EXPECT_GT(n, 500000u);
    mux.close();
}
#endif
