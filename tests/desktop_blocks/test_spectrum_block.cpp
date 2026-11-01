#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <thread>
#include "cler.hpp"
#include "desktop_blocks/spectrum/spectrum.hpp"

namespace {

void push_tone(cler::Channel<std::complex<float>>& ch, size_t n, double f_hz, double fs, size_t& phase_i) {
    for (size_t i = 0; i < n; ++i, ++phase_i) {
        const double ph = 2.0 * M_PI * f_hz * static_cast<double>(phase_i) / fs;
        ch.push({static_cast<float>(std::cos(ph)), static_cast<float>(std::sin(ph))});
    }
}

}  // namespace

TEST(SpectrumBlock, TonePeaksInTheRightBinOnAFixedScale) {
    const double fs = 1e6;
    SpectrumBlock spec("s", fs, 1024, 1000.0f, -120.0f, 0.5f, 4, SpectralWindow::Hann, 1 << 16);
    spec.set_center(100e6);
    spec.set_gen(7);
    cler::Channel<SpectrumFrame> out(16);
    size_t ph = 0;
    push_tone(spec.in, 8192, 100e3, fs, ph);

    ASSERT_TRUE(spec.procedure(&out).is_ok());
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(spec.in.size(), 0u);
    SpectrumFrame fr;
    out.pop(fr);
    EXPECT_EQ(fr.gen, 7u);
    EXPECT_DOUBLE_EQ(fr.center_hz, 100e6);
    EXPECT_DOUBLE_EQ(fr.rate_hz, fs);
    EXPECT_EQ(fr.n, 1024);
    EXPECT_FLOAT_EQ(fr.db_min, -120.0f);

    size_t peak = 0;
    for (size_t i = 1; i < fr.n; ++i) if (fr.bins[i] > fr.bins[peak]) peak = i;
    const size_t expect = 512 + static_cast<size_t>(std::lround(100e3 / fs * 1024));
    EXPECT_NEAR(static_cast<double>(peak), static_cast<double>(expect), 1.0);
    // full-scale tone = 0 dBFS -> (0 - -120) / 0.5 = 240
    EXPECT_NEAR(static_cast<double>(fr.bins[peak]), 240.0, 4.0);
    // Hann sidelobes: far bins are well below the peak
    EXPECT_LT(fr.bins[100], fr.bins[peak] - 100);
}

// a scheduler hands out small spans; frames must still come out
TEST(SpectrumBlock, AccumulatesAcrossSmallSpans) {
    const double fs = 1e6;
    SpectrumBlock spec("s", fs, 1024, 1000.0f, -120.0f, 0.5f, 4, SpectralWindow::Hann, 1 << 16);
    cler::Channel<SpectrumFrame> out(16);
    size_t ph = 0;
    for (int k = 0; k < 100; ++k) {
        push_tone(spec.in, 400, 100e3, fs, ph);
        ASSERT_TRUE(spec.procedure(&out).is_ok());
        EXPECT_EQ(spec.in.size(), 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GE(out.size(), 8u);
    SpectrumFrame fr;
    out.pop(fr);
    size_t peak = 0;
    for (size_t i = 1; i < fr.n; ++i) if (fr.bins[i] > fr.bins[peak]) peak = i;
    EXPECT_NEAR(static_cast<double>(peak), 614.0, 1.0);
}

TEST(SpectrumBlock, DefaultConstructionWorks) {
    SpectrumBlock spec("s", 2.4e6);
    cler::Channel<SpectrumFrame> out(16);
    size_t ph = 0;
    push_tone(spec.in, 4096, 0.0, 2.4e6, ph);
    ASSERT_TRUE(spec.procedure(&out).is_ok());
    EXPECT_EQ(out.size(), 1u);
}

TEST(SpectrumBlock, DrainsInputWhenFpsCapped) {
    const double fs = 1e6;
    SpectrumBlock spec("s", fs, 1024, 2.0f, -120.0f, 0.5f, 1, SpectralWindow::Hann, 1 << 16);
    cler::Channel<SpectrumFrame> out(16);
    size_t ph = 0;
    push_tone(spec.in, 4096, 10e3, fs, ph);
    ASSERT_TRUE(spec.procedure(&out).is_ok());
    EXPECT_EQ(out.size(), 1u);

    push_tone(spec.in, 4096, 10e3, fs, ph);
    ASSERT_TRUE(spec.procedure(&out).is_ok());
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(spec.in.size(), 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    push_tone(spec.in, 4096, 10e3, fs, ph);
    ASSERT_TRUE(spec.procedure(&out).is_ok());
    EXPECT_EQ(out.size(), 2u);

    EXPECT_EQ(spec.procedure(&out).unwrap_err(), cler::Error::NotEnoughSamples);
}

TEST(SpectrumBlock, DrainsInputWhenOutputIsFull) {
    const double fs = 1e6;
    SpectrumBlock spec("s", fs, 1024, 1000.0f, -120.0f, 0.5f, 1, SpectralWindow::Hann, 1 << 16);
    cler::Channel<SpectrumFrame> out(16);
    size_t ph = 0;
    for (int k = 0; k < 20; ++k) {
        push_tone(spec.in, 2048, 10e3, fs, ph);
        ASSERT_TRUE(spec.procedure(&out).is_ok());
        EXPECT_EQ(spec.in.size(), 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(out.space(), 0u);
    EXPECT_GE(out.size(), 15u);
}
