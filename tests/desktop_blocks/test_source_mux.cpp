#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <complex>
#include "cler.hpp"
#include "desktop_blocks/sources/source_mux.hpp"
#include "desktop_blocks/sigmf/sink_sigmf.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace {

void expect_paced(size_t n, double expected) {
    EXPECT_LE(static_cast<double>(n), expected * 1.1);
    EXPECT_GE(static_cast<double>(n), expected * 0.6);
}

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
    expect_paced(n, 1e6);

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
    expect_paced(n, 125e3);
}

TEST(SourceMux, EnumerationAlwaysOffersTheSimulator) {
    SourceMux mux("mux");
    auto devs = mux.enumerate();
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

TEST(SourceMux, SelectOfAMissingBackendFailsCleanly) {
    SourceMux mux("mux");
    EXPECT_FALSE(mux.select(SourceMux::Kind::Cariboulite, "nope", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
    EXPECT_FALSE(mux.select(SourceMux::Kind::Pluto, "usb:99.99.9", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
#ifdef CLER_HAS_UHD
    EXPECT_FALSE(mux.select(SourceMux::Kind::UHD, "serial=no-such-usrp", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
#endif
#ifdef CLER_HAS_SOAPYSDR
    EXPECT_FALSE(mux.select(SourceMux::Kind::Soapy, "driver=nosuchdriver", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
#endif
#ifdef CLER_HAS_HACKRF
    EXPECT_FALSE(mux.select(SourceMux::Kind::HackRF, "no-such-serial", 100e6, 2.4e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
#endif
    EXPECT_FALSE(mux.probe(SourceMux::Kind::Cariboulite, "nope"));
    EXPECT_FALSE(mux.probe(SourceMux::Kind::Pluto, "usb:99.99.9"));
    EXPECT_TRUE(mux.probe(SourceMux::Kind::Sim, ""));
    EXPECT_TRUE(mux.select(SourceMux::Kind::Sim, "", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::Sim);
}

TEST(SourceMux, SimSelectCapabilitiesSetAndSamples) {
    SourceMux mux("mux");
    ASSERT_TRUE(mux.select(SourceMux::Kind::Sim, "", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::Sim);
    EXPECT_DOUBLE_EQ(mux.rate(), 2e6);
    EXPECT_DOUBLE_EQ(mux.center(), 100e6);
    EXPECT_FALSE(mux.lost());

    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 4u);
    EXPECT_EQ(caps[0].id, "freq");
    EXPECT_EQ(caps[0].type, "range");
    EXPECT_DOUBLE_EQ(caps[0].value, 100e6);
    EXPECT_FALSE(caps[0].ro);
    EXPECT_EQ(caps[1].id, "rate");
    EXPECT_TRUE(caps[1].ro);
    EXPECT_EQ(caps[2].id, "tone_hz");

    mux.set("freq", 101e6);
    mux.set("tone_hz", 250e3);
    mux.set("snr_db", 10.0);
    EXPECT_DOUBLE_EQ(mux.center(), 101e6);
    EXPECT_DOUBLE_EQ(mux.capabilities()[2].value, 250e3);
    EXPECT_FLOAT_EQ(static_cast<float>(mux.capabilities()[3].value), 10.0f);

    const size_t n = pump(mux, 0.3);
    expect_paced(n, 600e3);

    ASSERT_TRUE(mux.select(SourceMux::Kind::Sim, "", 50e6, 1e6));
    EXPECT_DOUBLE_EQ(mux.rate(), 1e6);
    EXPECT_DOUBLE_EQ(mux.center(), 50e6);
    mux.close();
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
}

#ifdef CLER_HAS_HACKRF
TEST(SourceMux, HackRFIfPresent) {
    SourceMux mux("mux");
    auto devs = mux.enumerate();
    const SourceMux::DeviceInfo* hack = nullptr;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::HackRF) hack = &d;
    if (!hack) GTEST_SKIP() << "no HackRF connected";

    ASSERT_TRUE(mux.select(SourceMux::Kind::HackRF, hack->id, 100e6, 2.4e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::HackRF);
    EXPECT_DOUBLE_EQ(mux.rate(), 2.4e6);
    EXPECT_FALSE(mux.lost());
    auto again = mux.enumerate();
    size_t hacks = 0;
    bool sim = false;
    for (auto& d : again) {
        if (d.kind == SourceMux::Kind::HackRF) { ++hacks; EXPECT_EQ(d.id, hack->id); }
        if (d.kind == SourceMux::Kind::Sim) sim = true;
    }
    EXPECT_EQ(hacks, 1u);
    EXPECT_TRUE(sim);
    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 5u);
    EXPECT_EQ(caps[1].type, "enum");
    EXPECT_TRUE(caps[1].ro);
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

#ifdef CLER_HAS_CARIBOULITE
TEST(SourceMux, CaribouliteIfPresent) {
    SourceMux mux("mux");
    auto devs = mux.enumerate();
    const SourceMux::DeviceInfo* s1g = nullptr;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::Cariboulite && d.id == "s1g") s1g = &d;
    if (!s1g) GTEST_SKIP() << "no CaribouLite board";

    ASSERT_TRUE(mux.select(SourceMux::Kind::Cariboulite, "s1g", 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::Cariboulite);
    EXPECT_GT(mux.rate(), 0.0);
    EXPECT_FALSE(mux.lost());
    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 5u);
    EXPECT_EQ(caps[0].id, "freq");
    EXPECT_GT(caps[0].min, 100e6);
    EXPECT_GE(mux.center(), caps[0].min);
    EXPECT_LE(mux.center(), caps[0].max);
    EXPECT_EQ(caps[1].id, "rate");
    EXPECT_TRUE(caps[1].ro);
    EXPECT_EQ(caps[2].id, "gain");
    EXPECT_GT(caps[2].step, 0.0);
    EXPECT_EQ(caps[3].type, "bool");
    const double f = caps[0].min + 10e6;
    mux.set("freq", f);
    EXPECT_NEAR(mux.center(), f, 1e3);
    mux.set("freq", 100e6);
    EXPECT_NEAR(mux.center(), f, 1e3);
    const size_t n = pump(mux, 0.5);
    EXPECT_GT(n, 100000u);
    mux.close();
    EXPECT_EQ(mux.kind(), SourceMux::Kind::None);
}
#endif

#ifdef CLER_HAS_LIBIIO
TEST(SourceMux, PlutoIfPresent) {
    SourceMux mux("mux");
    auto devs = mux.enumerate();
    const SourceMux::DeviceInfo* pluto = nullptr;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::Pluto) pluto = &d;
    if (!pluto) GTEST_SKIP() << "no Pluto connected";

    ASSERT_TRUE(mux.select(SourceMux::Kind::Pluto, pluto->id, 100.5e6, 2.4e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::Pluto);
    EXPECT_NEAR(mux.rate(), 2.4e6, 2.4e4);
    EXPECT_NEAR(mux.center(), 100.5e6, 1e3);
    EXPECT_FALSE(mux.lost());
    auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 4u);
    EXPECT_EQ(caps[0].id, "freq");
    EXPECT_LE(caps[0].min, 100.5e6);
    EXPECT_EQ(caps[1].id, "rate");
    EXPECT_TRUE(caps[1].ro);
    EXPECT_EQ(caps[2].id, "gain");
    EXPECT_EQ(caps[3].id, "agc");
    EXPECT_EQ(caps[3].type, "bool");
    mux.set("gain", 40);
    EXPECT_NEAR(mux.capabilities()[2].value, 40.0, 1.0);
    mux.set("freq", 101.1e6);
    EXPECT_NEAR(mux.center(), 101.1e6, 1e3);
    const size_t n = pump(mux, 0.5);
    EXPECT_GT(n, 500000u);
    mux.close();
}
#endif

#ifdef CLER_HAS_UHD
TEST(SourceMux, UHDIfPresent) {
    SourceMux mux("mux");
    auto devs = mux.enumerate();
    const SourceMux::DeviceInfo* usrp = nullptr;
    for (auto& d : devs) if (d.kind == SourceMux::Kind::UHD) usrp = &d;
    if (!usrp) GTEST_SKIP() << "no USRP connected";

    ASSERT_TRUE(mux.select(SourceMux::Kind::UHD, usrp->id, 100e6, 2e6));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::UHD);
    EXPECT_GT(mux.rate(), 0.0);
    auto caps = mux.capabilities();
    ASSERT_GE(caps.size(), 3u);
    EXPECT_EQ(caps[0].id, "freq");
    EXPECT_EQ(caps[1].id, "rate");
    EXPECT_TRUE(caps[1].ro);
    EXPECT_EQ(caps[2].id, "gain");
    const size_t n = pump(mux, 0.5);
    EXPECT_GT(n, 100000u);
    mux.close();
}
#endif

#ifdef CLER_HAS_SOAPYSDR
TEST(SourceMux, SoapyEnumerateSkipsNativeDrivers) {
    SourceMux mux("mux");
    for (const auto& d : mux.enumerate()) {
        if (d.kind != SourceMux::Kind::Soapy) continue;
#ifdef CLER_HAS_HACKRF
        EXPECT_EQ(d.id.find("driver=hackrf"), std::string::npos) << d.id;
#endif
#ifdef CLER_HAS_LIBIIO
        EXPECT_EQ(d.id.find("driver=plutosdr"), std::string::npos) << d.id;
#endif
#ifdef CLER_HAS_UHD
        EXPECT_EQ(d.id.find("driver=uhd"), std::string::npos) << d.id;
#endif
    }
}
#endif
TEST(SourceMux, SigMFEnumerateSelectAndTransport) {
    const std::string dir = testing::TempDir();
    const std::string base = dir + "/muxplay";
    {
        SinkSigMFBlock<std::complex<float>> sink("s", base.c_str(), 1e5, 433e6, sigmf::Datatype::ci16_le);
        std::vector<std::complex<float>> tone(1000, {0.5f, -0.25f});
        sink.in.writeN(tone.data(), tone.size());
        ASSERT_TRUE(sink.procedure().is_ok());
    }
    {
        FILE* bad = std::fopen((dir + "/bad.sigmf-meta").c_str(), "wb");
        ASSERT_NE(bad, nullptr);
        const char* txt = "{\n  \"global\": { \"core:datatype\": \"cf64_le\", \"core:sample_rate\": 1e6 }\n}\n";
        std::fwrite(txt, 1, std::strlen(txt), bad);
        std::fclose(bad);
        FILE* dat = std::fopen((dir + "/bad.sigmf-data").c_str(), "wb");
        std::fclose(dat);
    }
    SourceMux mux("mux");
    mux.set_sigmf_dir(dir);
    EXPECT_FALSE(mux.select(SourceMux::Kind::SigMF, "bad", 0, 0));
    bool listed = false;
    bool bad_listed = false;
    for (const auto& d : mux.enumerate()) {
        listed = listed || (d.kind == SourceMux::Kind::SigMF && d.id == "muxplay");
        bad_listed = bad_listed || d.id == "bad";
    }
    EXPECT_TRUE(listed);
    EXPECT_FALSE(bad_listed);

    EXPECT_FALSE(mux.select(SourceMux::Kind::SigMF, "../muxplay", 0, 0));
    EXPECT_FALSE(mux.select(SourceMux::Kind::SigMF, "missing", 0, 0));
    ASSERT_TRUE(mux.select(SourceMux::Kind::SigMF, "muxplay", 0, 0));
    EXPECT_EQ(mux.kind(), SourceMux::Kind::SigMF);
    EXPECT_TRUE(mux.is_file());
    EXPECT_DOUBLE_EQ(mux.rate(), 1e5);
    EXPECT_DOUBLE_EQ(mux.center(), 433e6);
    EXPECT_DOUBLE_EQ(mux.duration_seconds(), 0.01);

    const auto caps = mux.capabilities();
    ASSERT_EQ(caps.size(), 2u);
    EXPECT_TRUE(caps[0].ro);
    EXPECT_TRUE(caps[1].ro);

    const size_t n = pump(mux, 0.05);
    expect_paced(n, 1000);
    EXPECT_TRUE(mux.ended());
    mux.set_loop(true);
    mux.seek(0.0);
    cler::Channel<std::complex<float>> out(1 << 16);
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
    while (std::chrono::steady_clock::now() < end) { mux.procedure(&out); out.commit_read(out.size()); }
    EXPECT_FALSE(mux.ended());
    mux.pause(true);
    EXPECT_TRUE(mux.paused());
    mux.pause(false);
    mux.set_loop(false);
    {
        cler::Channel<std::complex<float>> drain2(1 << 16);
        const auto e2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
        while (std::chrono::steady_clock::now() < e2 && !mux.ended()) { mux.procedure(&drain2); drain2.commit_read(drain2.size()); }
        EXPECT_TRUE(mux.ended());
        mux.set_loop(true);
        const auto e3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        size_t resumed = 0;
        while (std::chrono::steady_clock::now() < e3) { mux.procedure(&drain2); resumed += drain2.size(); drain2.commit_read(drain2.size()); }
        EXPECT_FALSE(mux.ended());
        EXPECT_GT(resumed, 0u);
    }
    std::remove((dir + "/bad.sigmf-meta").c_str());
    std::remove((dir + "/bad.sigmf-data").c_str());
    std::remove((base + ".sigmf-meta").c_str());
    std::remove((base + ".sigmf-data").c_str());
}
