#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <cmath>

#include "cler.hpp"
#include "desktop_blocks/noise/clock_drift.hpp"

using cf = std::complex<float>;

namespace {

constexpr double kTwoPi = 6.283185307179586;

std::vector<cf> tone(size_t n, double cycles_per_sample) {
    std::vector<cf> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double phase = kTwoPi * std::fmod(cycles_per_sample * static_cast<double>(i), 1.0);
        x[i] = cf(static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)));
    }
    return x;
}

// Feeds input[begin, end) through the block and appends everything it emits to output.
void run(ClockDriftBlock& blk, cler::Channel<cf>& out, const std::vector<cf>& input,
    size_t begin, size_t end, std::vector<cf>& output) {
    std::vector<cf> drain(out.space() + out.size());
    size_t fed = begin;
    int idle = 0;
    // read_dbf spans are lower bounds: a retryable error means "call again", so stop
    // only after two calls in a row made no progress once all input was fed.
    while (idle < 2) {
        if (fed < end) fed += blk.in.writeN(input.data() + fed, std::min(blk.in.space(), end - fed));
        auto r = blk.procedure(&out);
        const size_t got = out.readN(drain.data(), out.size());
        output.insert(output.end(), drain.begin(), drain.begin() + got);
        idle = (r.is_err() && fed == end && got == 0) ? idle + 1 : 0;
    }
}

// Phase slope (rad/sample) of y[n] * conj(x[n]) measured over a lag of L samples.
double phase_slope(const std::vector<cf>& y, const std::vector<cf>& ref, size_t start, size_t lag) {
    std::complex<double> acc{0.0, 0.0};
    for (size_t n = start; n + lag < y.size(); ++n) {
        const std::complex<double> z0 = std::complex<double>(y[n]) * std::conj(std::complex<double>(ref[n]));
        const std::complex<double> z1 = std::complex<double>(y[n + lag]) * std::conj(std::complex<double>(ref[n + lag]));
        acc += z1 * std::conj(z0);
    }
    return std::arg(acc) / static_cast<double>(lag);
}

}  // namespace

TEST(ClockDriftBlockTest, ZeroPpmZeroOffsetIsIdentity) {
    const size_t n = 20000;
    std::vector<cf> input(n);
    for (size_t i = 0; i < n; ++i) {
        input[i] = cf(std::sin(0.37f * static_cast<float>(i)), std::cos(0.11f * static_cast<float>(i) + 0.5f));
    }
    ClockDriftBlock blk("drift", 0.0, 0.0);
    cler::Channel<cf> out(8192);
    std::vector<cf> output;
    run(blk, out, input, 0, n, output);

    // The interpolator needs two samples of look-ahead, so the last two inputs stay buffered.
    ASSERT_EQ(output.size(), n - 2);
    float max_err = 0.0f;
    for (size_t i = 0; i < output.size(); ++i) {
        max_err = std::max(max_err, std::abs(output[i] - input[i]));
    }
    EXPECT_LT(max_err, 1e-6f);
}

TEST(ClockDriftBlockTest, OutputCountMatchesRatio) {
    const size_t n = 1000000;
    const std::vector<cf> input = tone(n, 0.01);
    for (double ppm : {100.0, -100.0}) {
        ClockDriftBlock blk("drift", ppm, 0.0);
        cler::Channel<cf> out(8192);
        std::vector<cf> output;
        run(blk, out, input, 0, n, output);
        // The last two inputs stay buffered as interpolator look-ahead.
        const double expected = static_cast<double>(n - 2) / (1.0 + ppm * 1e-6);
        EXPECT_NEAR(static_cast<double>(output.size()), expected, 2.0) << "ppm " << ppm;
    }
}

TEST(ClockDriftBlockTest, ToneFrequencyScalesWithRatio) {
    const double f = 0.01;
    const double ppm = 100.0;
    const double r = 1.0 + ppm * 1e-6;
    const size_t n = 200000;
    const std::vector<cf> input = tone(n, f);
    ClockDriftBlock blk("drift", ppm, 0.0);
    cler::Channel<cf> out(8192);
    std::vector<cf> output;
    run(blk, out, input, 0, n, output);

    const std::vector<cf> ref = tone(output.size(), f);
    const double measured_shift = phase_slope(output, ref, 0, 100000) / kTwoPi;
    const double expected_shift = f * (r - 1.0);
    EXPECT_NEAR(measured_shift / expected_shift, 1.0, 0.01);
}

TEST(ClockDriftBlockTest, TimingOffsetShiftsPhase) {
    const double f = 0.01;
    const size_t n = 50000;
    const std::vector<cf> input = tone(n, f);
    ClockDriftBlock blk("drift", 0.0, 0.5);
    cler::Channel<cf> out(8192);
    std::vector<cf> output;
    run(blk, out, input, 0, n, output);

    std::complex<double> acc{0.0, 0.0};
    for (size_t i = 4; i < output.size(); ++i) {
        acc += std::complex<double>(output[i]) * std::conj(std::complex<double>(input[i]));
    }
    EXPECT_NEAR(std::arg(acc), kTwoPi * f * 0.5, 1e-3);
}

TEST(ClockDriftBlockTest, LivePpmChangeHasNoGapOrDuplicate) {
    const double f = 0.01;
    const size_t n = 400000;
    const std::vector<cf> input = tone(n, f);
    ClockDriftBlock blk("drift", 0.0, 0.0);
    cler::Channel<cf> out(8192);
    std::vector<cf> output;
    run(blk, out, input, 0, n / 2, output);
    const double t_switch = blk.accumulated_time();
    const size_t n_before = output.size();
    blk.set_ppm(500.0);
    run(blk, out, input, n / 2, n, output);

    // Every output step advances the tone by 2*pi*f*r with r = 1 or 1 + 500e-6:
    // a dropped or repeated sample would show up as a step of ~0 or ~2x.
    double worst = 0.0;
    for (size_t i = 1; i < output.size(); ++i) {
        const double step = std::arg(std::complex<double>(output[i]) * std::conj(std::complex<double>(output[i - 1])));
        worst = std::max(worst, std::abs(step / (kTwoPi * f) - 1.0));
    }
    EXPECT_LT(worst, 1e-3);

    const double r = 1.0 + 500e-6;
    const double expected_after = (static_cast<double>(n) - 2.0 - t_switch) / r;
    EXPECT_NEAR(static_cast<double>(output.size() - n_before), expected_after, 2.0);
    EXPECT_NEAR(blk.accumulated_time(), t_switch + static_cast<double>(output.size() - n_before) * r, 1e-6);
}
