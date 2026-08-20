#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <algorithm>
#include <vector>
#include "cler.hpp"
#include "desktop_blocks/demod/analog_demod.hpp"

namespace {

constexpr double kChannelRate = 240e3;

std::vector<float> run(AnalogDemodBlock& d, const std::vector<std::complex<float>>& iq) {
    cler::Channel<float> out(1 << 16);
    std::vector<float> audio;
    size_t pos = 0;
    while (pos < iq.size()) {
        auto [w, ws] = d.in.write_dbf();
        size_t n = std::min(ws, iq.size() - pos);
        for (size_t i = 0; i < n; ++i) w[i] = iq[pos + i];
        d.in.commit_write(n);
        pos += n;
        while (d.procedure(&out).is_ok()) {
            auto [r, rs] = out.read_dbf();
            audio.insert(audio.end(), r, r + rs);
            out.commit_read(rs);
        }
    }
    return audio;
}

// audio-band tone power at f0 via Goertzel-ish correlation, skipping settling
double tone_power(const std::vector<float>& a, double f0, double fs) {
    const size_t skip = a.size() / 4;
    double c = 0, s = 0;
    for (size_t i = skip; i < a.size(); ++i) {
        c += a[i] * std::cos(2.0 * M_PI * f0 * i / fs);
        s += a[i] * std::sin(2.0 * M_PI * f0 * i / fs);
    }
    const double n = static_cast<double>(a.size() - skip);
    return (c * c + s * s) * 4.0 / (n * n);
}

double total_power(const std::vector<float>& a) {
    const size_t skip = a.size() / 4;
    double p = 0;
    for (size_t i = skip; i < a.size(); ++i) p += a[i] * a[i];
    return p / static_cast<double>(a.size() - skip);
}

}  // namespace

TEST(AnalogDemod, WbfmRecoversTone) {
    AnalogDemodBlock d("d", kChannelRate, AnalogDemodBlock::Mode::WBFM, 1 << 16);
    std::vector<std::complex<float>> iq(static_cast<size_t>(kChannelRate));
    double phase = 0.0;
    for (size_t i = 0; i < iq.size(); ++i) {
        const double m = std::sin(2.0 * M_PI * 1000.0 * i / kChannelRate);
        phase += 2.0 * M_PI * 75e3 * m / kChannelRate;
        iq[i] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
    }
    auto a = run(d, iq);
    ASSERT_GT(a.size(), 20000u);
    // 50 us de-emphasis leaves a 1 kHz tone at ~0.95 of the deviation-normalised level
    const double p = tone_power(a, 1000.0, 48000.0);
    EXPECT_GT(p, 0.2);
    EXPECT_GT(p / (total_power(a) + 1e-12), 0.9);
}

// A 26 kHz modulating tone at 768 kHz (decim 16) must be stopped by the audio
// decimator, not folded to 48-26 = 22 kHz. A transition width fixed in
// normalised units leaves only ~20 dB of rejection at 24 kHz at this rate.
TEST(AnalogDemod, WbfmDecimatorRejectsAlias) {
    AnalogDemodBlock d("d", 768e3, AnalogDemodBlock::Mode::WBFM, 1 << 16);
    std::vector<std::complex<float>> iq(static_cast<size_t>(768e3));
    double phase = 0.0;
    for (size_t i = 0; i < iq.size(); ++i) {
        const double m = std::sin(2.0 * M_PI * 26e3 * i / 768e3);
        phase += 2.0 * M_PI * 75e3 * m / 768e3;
        iq[i] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
    }
    auto a = run(d, iq);
    ASSERT_GT(a.size(), 20000u);
    EXPECT_LT(tone_power(a, 22000.0, 48000.0), 1e-5);
}

TEST(AnalogDemod, AmRecoversTone) {
    AnalogDemodBlock d("d", kChannelRate, AnalogDemodBlock::Mode::AM, 1 << 16);
    std::vector<std::complex<float>> iq(static_cast<size_t>(kChannelRate));
    for (size_t i = 0; i < iq.size(); ++i) {
        const double m = 1.0 + 0.6 * std::sin(2.0 * M_PI * 800.0 * i / kChannelRate);
        iq[i] = {static_cast<float>(0.5 * m), 0.0f};
    }
    auto a = run(d, iq);
    const double p = tone_power(a, 800.0, 48000.0);
    EXPECT_GT(p, 0.05);
    EXPECT_GT(p / (total_power(a) + 1e-12), 0.9);
}

TEST(AnalogDemod, AmSwitchOnCarrierDoesNotThump) {
    AnalogDemodBlock d("d", kChannelRate, AnalogDemodBlock::Mode::AM, 1 << 16);
    std::vector<std::complex<float>> iq(static_cast<size_t>(kChannelRate / 4), {0.5f, 0.0f});
    auto a = run(d, iq);
    ASSERT_GT(a.size(), 1000u);
    float peak = 0.0f;
    for (float v : a) peak = std::max(peak, std::fabs(v));
    EXPECT_LT(peak, 0.1f);
}

TEST(AnalogDemod, UsbRecoversToneAndRejectsLsb) {
    // a USB voice tone at +1 kHz: complex exponential at +1 kHz
    AnalogDemodBlock d("d", kChannelRate, AnalogDemodBlock::Mode::USB, 1 << 16);
    std::vector<std::complex<float>> iq(static_cast<size_t>(kChannelRate));
    for (size_t i = 0; i < iq.size(); ++i) {
        const double ph = 2.0 * M_PI * 1000.0 * i / kChannelRate;
        iq[i] = {static_cast<float>(0.5 * std::cos(ph)), static_cast<float>(0.5 * std::sin(ph))};
    }
    auto a = run(d, iq);
    const double p = tone_power(a, 1000.0, 48000.0);
    EXPECT_GT(p, 0.02);
    EXPECT_GT(p / (total_power(a) + 1e-12), 0.8);

    // the same signal on the wrong sideband is rejected
    AnalogDemodBlock d2("d2", kChannelRate, AnalogDemodBlock::Mode::LSB, 1 << 16);
    auto b = run(d2, iq);
    EXPECT_LT(total_power(b), total_power(a) * 0.05);
}

TEST(AnalogDemod, ModeSwitchTakesEffect) {
    AnalogDemodBlock d("d", kChannelRate, AnalogDemodBlock::Mode::AM, 1 << 16);
    EXPECT_EQ(d.mode(), AnalogDemodBlock::Mode::AM);
    d.set_mode(AnalogDemodBlock::Mode::NBFM);
    std::vector<std::complex<float>> iq(static_cast<size_t>(kChannelRate / 2));
    double phase = 0.0;
    for (size_t i = 0; i < iq.size(); ++i) {
        phase += 2.0 * M_PI * 2.5e3 * std::sin(2.0 * M_PI * 700.0 * i / kChannelRate) / kChannelRate;
        iq[i] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
    }
    auto a = run(d, iq);
    const double p = tone_power(a, 700.0, 48000.0);
    EXPECT_GT(p / (total_power(a) + 1e-12), 0.8);
}
