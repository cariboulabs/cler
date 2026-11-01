#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <cmath>

#include "cler.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"

class ResamplerBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    // Helper to generate test signal (sine wave)
    std::vector<float> generate_sine_wave(size_t num_samples, float frequency, float sample_rate) {
        std::vector<float> signal(num_samples);
        for (size_t i = 0; i < num_samples; i++) {
            float t = static_cast<float>(i) / sample_rate;
            signal[i] = std::sin(2.0f * M_PI * frequency * t);
        }
        return signal;
    }
    
    // Helper to generate complex test signal (complex exponential)
    std::vector<std::complex<float>> generate_complex_exponential(size_t num_samples, float frequency, float sample_rate) {
        std::vector<std::complex<float>> signal(num_samples);
        for (size_t i = 0; i < num_samples; i++) {
            float t = static_cast<float>(i) / sample_rate;
            float phase = 2.0f * M_PI * frequency * t;
            signal[i] = std::complex<float>(std::cos(phase), std::sin(phase));
        }
        return signal;
    }
};

// Test MultiStageResamplerBlock upsampling (float)
TEST_F(ResamplerBlocksTest, MultiStageResamplerFloatUpsample) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 2.0f; // Upsample by 2x
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<float> resampler("test_resampler_up", ratio, attenuation, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Generate input test signal (10 Hz sine at 100 Hz sample rate)
    auto input_data = generate_sine_wave(100, 10.0f, 100.0f);
    
    for (float sample : input_data) {
        resampler.in.push(sample);
    }
    
    // Process samples
    auto result = resampler.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify we got approximately 2x more output samples
    // Note: The exact count may vary due to resampler initialization and filter transients
    size_t output_size = output.size();
    EXPECT_GT(output_size, input_data.size() * 1.5f) << "Upsampling should increase sample count";
    EXPECT_LT(output_size, input_data.size() * 2.5f) << "Output samples should be reasonable";
    
    // Collect output data for basic verification
    std::vector<float> output_data;
    float sample;
    while (output.try_pop(sample)) {
        output_data.push_back(sample);
    }
    
    // Basic sanity check - output should not be all zeros or NaN
    bool has_nonzero = false;
    bool has_valid = true;
    for (float val : output_data) {
        if (std::abs(val) > 1e-6) has_nonzero = true;
        if (!std::isfinite(val)) has_valid = false;
    }
    
    EXPECT_TRUE(has_nonzero) << "Output should contain non-zero values";
    EXPECT_TRUE(has_valid) << "All output values should be finite";
}

// Test MultiStageResamplerBlock downsampling (float)
TEST_F(ResamplerBlocksTest, MultiStageResamplerFloatDownsample) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 0.5f; // Downsample by 2x
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<float> resampler("test_resampler_down", ratio, attenuation, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Generate input test signal (10 Hz sine at 100 Hz sample rate)
    auto input_data = generate_sine_wave(200, 10.0f, 100.0f);
    
    for (float sample : input_data) {
        resampler.in.push(sample);
    }
    
    // Process samples
    auto result = resampler.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify we got approximately half the output samples
    size_t output_size = output.size();
    EXPECT_GT(output_size, input_data.size() * 0.3f) << "Downsampling should reduce sample count";
    EXPECT_LT(output_size, input_data.size() * 0.7f) << "Output samples should be reasonable";
    
    // Collect output data for basic verification
    std::vector<float> output_data;
    float sample;
    while (output.try_pop(sample)) {
        output_data.push_back(sample);
    }
    
    // Basic sanity check - output should not be all zeros or NaN
    bool has_nonzero = false;
    bool has_valid = true;
    for (float val : output_data) {
        if (std::abs(val) > 1e-6) has_nonzero = true;
        if (!std::isfinite(val)) has_valid = false;
    }
    
    EXPECT_TRUE(has_nonzero) << "Output should contain non-zero values";
    EXPECT_TRUE(has_valid) << "All output values should be finite";
}

// Test MultiStageResamplerBlock with complex float upsampling
TEST_F(ResamplerBlocksTest, MultiStageResamplerComplexUpsample) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 1.5f; // Upsample by 1.5x
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<std::complex<float>> resampler("test_resampler_complex", ratio, attenuation, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Generate complex exponential test signal
    auto input_data = generate_complex_exponential(100, 10.0f, 100.0f);
    
    for (const auto& sample : input_data) {
        resampler.in.push(sample);
    }
    
    // Process samples
    auto result = resampler.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify we got approximately 1.5x more output samples
    size_t output_size = output.size();
    EXPECT_GT(output_size, input_data.size() * 1.2f) << "Upsampling should increase sample count";
    EXPECT_LT(output_size, input_data.size() * 1.8f) << "Output samples should be reasonable";
    
    // Collect output data for basic verification
    std::vector<std::complex<float>> output_data;
    std::complex<float> sample;
    while (output.try_pop(sample)) {
        output_data.push_back(sample);
    }
    
    // Basic sanity check - output should not be all zeros or NaN
    bool has_nonzero = false;
    bool has_valid = true;
    for (const auto& val : output_data) {
        if (std::abs(val) > 1e-6) has_nonzero = true;
        if (!std::isfinite(val.real()) || !std::isfinite(val.imag())) has_valid = false;
    }
    
    EXPECT_TRUE(has_nonzero) << "Output should contain non-zero values";
    EXPECT_TRUE(has_valid) << "All output values should be finite";
}

// Test MultiStageResamplerBlock with complex float downsampling
TEST_F(ResamplerBlocksTest, MultiStageResamplerComplexDownsample) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 0.75f; // Downsample by 1.33x
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<std::complex<float>> resampler("test_resampler_complex_down", ratio, attenuation, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Generate complex exponential test signal
    auto input_data = generate_complex_exponential(200, 10.0f, 100.0f);
    
    for (const auto& sample : input_data) {
        resampler.in.push(sample);
    }
    
    // Process samples
    auto result = resampler.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify we got approximately 0.75x output samples
    size_t output_size = output.size();
    EXPECT_GT(output_size, input_data.size() * 0.6f) << "Downsampling should reduce sample count";
    EXPECT_LT(output_size, input_data.size() * 0.9f) << "Output samples should be reasonable";
    
    // Collect output data for basic verification
    std::vector<std::complex<float>> output_data;
    std::complex<float> sample;
    while (output.try_pop(sample)) {
        output_data.push_back(sample);
    }
    
    // Basic sanity check - output should not be all zeros or NaN
    bool has_nonzero = false;
    bool has_valid = true;
    for (const auto& val : output_data) {
        if (std::abs(val) > 1e-6) has_nonzero = true;
        if (!std::isfinite(val.real()) || !std::isfinite(val.imag())) has_valid = false;
    }
    
    EXPECT_TRUE(has_nonzero) << "Output should contain non-zero values";
    EXPECT_TRUE(has_valid) << "All output values should be finite";
}

// Test MultiStageResamplerBlock with unit ratio (should be passthrough)
TEST_F(ResamplerBlocksTest, MultiStageResamplerUnitRatio) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 1.0f; // No resampling
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<float> resampler("test_resampler_unit", ratio, attenuation, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Simple test data
    std::vector<float> input_data = {1.0f, -1.0f, 2.0f, -2.0f, 0.5f};
    
    for (float sample : input_data) {
        resampler.in.push(sample);
    }
    
    // Process samples
    auto result = resampler.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // With unit ratio, we should get approximately the same number of samples
    // (allowing for filter initialization effects)
    size_t output_size = output.size();
    EXPECT_GE(output_size, input_data.size() - 2) << "Unit ratio should preserve most samples";
    EXPECT_LE(output_size, input_data.size() + 2) << "Unit ratio should not create many extra samples";
    
    // Collect output for verification
    std::vector<float> output_data;
    float sample;
    while (output.try_pop(sample)) {
        output_data.push_back(sample);
    }
    
    // Output should be finite and reasonable
    for (float val : output_data) {
        EXPECT_TRUE(std::isfinite(val)) << "Output values should be finite";
        EXPECT_LT(std::abs(val), 10.0f) << "Output values should be reasonable magnitude";
    }
}

// Test MultiStageResamplerBlock error conditions
// 0.5 then 0.1 on the same block: the output count follows the new ratio and
// nothing from the old filter state leaks (finite, non-zero output)
TEST_F(ResamplerBlocksTest, MultiStageResamplerSetRatio) {
    const size_t buffer_size = 4096;
    MultiStageResamplerBlock<std::complex<float>> resampler("r", 0.5f, 60.0f, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    auto input = generate_complex_exponential(2000, 1000.0f, 100000.0f);

    for (auto v : input) resampler.in.push(v);
    EXPECT_TRUE(resampler.procedure(&output).is_ok());
    const size_t half = output.size();
    EXPECT_NEAR(static_cast<double>(half), 1000.0, 30.0);
    std::complex<float> tmp;
    while (output.try_pop(tmp)) {}

    resampler.set_ratio(0.1f);
    EXPECT_FLOAT_EQ(resampler.ratio(), 0.1f);
    for (auto v : input) resampler.in.push(v);
    EXPECT_TRUE(resampler.procedure(&output).is_ok());
    const size_t tenth = output.size();
    EXPECT_NEAR(static_cast<double>(tenth), 200.0, 20.0);
    bool finite = true, nonzero = false;
    while (output.try_pop(tmp)) {
        if (!std::isfinite(tmp.real())) finite = false;
        if (std::abs(tmp) > 1e-6f) nonzero = true;
    }
    EXPECT_TRUE(finite);
    EXPECT_TRUE(nonzero);
}

TEST_F(ResamplerBlocksTest, MultiStageResamplerErrorConditions) {
    const size_t buffer_size = 4096;
    const float attenuation = 60.0f;
    
    // Test buffer size too small for doubly-mapped buffers (need at least 4096/sizeof(float) = 1024 for float)
    EXPECT_DEATH(MultiStageResamplerBlock<float>("test", 2.0f, attenuation, 1), "cler panic");
    
    // NOTE: Cannot safely test invalid ratio/attenuation parameters because liquid-dsp
    // library calls exit() or segfaults instead of returning error codes that we can handle.
    // Our parameter validation prevents these calls, but gtest still somehow triggers them.
}

// Test MultiStageResamplerBlock with empty input
TEST_F(ResamplerBlocksTest, MultiStageResamplerEmptyInput) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 2.0f;
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<float> resampler("test_resampler_empty", ratio, attenuation, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Run with empty input
    auto result = resampler.procedure(&output);
    EXPECT_FALSE(result.is_ok()); // Should return NotEnoughSamples error
    EXPECT_EQ(output.size(), 0);
}

// Test MultiStageResamplerBlock multiple runs - verify continuity
TEST_F(ResamplerBlocksTest, MultiStageResamplerMultipleRuns) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float ratio = 2.0f; // Upsample by 2x
    const float attenuation = 60.0f;
    
    MultiStageResamplerBlock<float> resampler("test_resampler_multiple", ratio, attenuation, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // First batch
    std::vector<float> batch1 = {1.0f, 0.0f, -1.0f, 0.0f};
    for (float sample : batch1) {
        resampler.in.push(sample);
    }
    
    auto result1 = resampler.procedure(&output);
    EXPECT_TRUE(result1.is_ok());
    
    size_t first_output_size = output.size();
    EXPECT_GT(first_output_size, 0) << "First run should produce output";
    
    // Clear output
    float dummy;
    while (output.try_pop(dummy)) {}
    
    // Second batch
    std::vector<float> batch2 = {0.5f, -0.5f, 0.5f};
    for (float sample : batch2) {
        resampler.in.push(sample);
    }
    
    auto result2 = resampler.procedure(&output);
    EXPECT_TRUE(result2.is_ok());
    
    size_t second_output_size = output.size();
    EXPECT_GT(second_output_size, 0) << "Second run should produce output";
    
    // Verify output is still finite and reasonable
    std::vector<float> second_batch_output;
    while (output.try_pop(dummy)) {
        second_batch_output.push_back(dummy);
        EXPECT_TRUE(std::isfinite(dummy)) << "Output should remain finite across runs";
    }
}

// Test that small buffer triggers dbf exception
TEST_F(ResamplerBlocksTest, MultiStageResamplerSmallBufferException) {
    const size_t small_buffer = 1; // Too small for dbf (need at least 1024 for float)
    const float ratio = 2.0f;
    const float attenuation = 60.0f;
    
    // Buffer too small for doubly-mapped: must die
    EXPECT_DEATH(MultiStageResamplerBlock<float>("test_resampler_small", ratio, attenuation, small_buffer), "cler panic");
}
namespace {

constexpr size_t kRatInterp = 5;
constexpr size_t kRatDecim  = 6;
constexpr size_t kRatTaps   = 14;
constexpr float  kRatAtten  = 80.0f;

using RatBlock  = RationalResamplerBlock<kRatInterp, kRatDecim, kRatTaps>;
using RatKernel = RationalResampler<kRatInterp, kRatDecim, kRatTaps>;

std::vector<std::complex<float>> drain(cler::Channel<std::complex<float>>& ch) {
    std::vector<std::complex<float>> out;
    std::complex<float> s;
    while (ch.try_pop(s)) out.push_back(s);
    return out;
}

std::vector<std::complex<float>> run_block_in_chunks(RatBlock& block,
                                                     cler::Channel<std::complex<float>>& out,
                                                     const std::vector<std::complex<float>>& input,
                                                     size_t chunk) {
    std::vector<std::complex<float>> collected;
    size_t pos = 0;
    while (pos < input.size()) {
        const size_t n = std::min(chunk, input.size() - pos);
        const size_t written = block.in.writeN(input.data() + pos, n);
        pos += written;
        while (block.in.size() > 0) {
            if (block.procedure(&out).is_err()) break;
            auto got = drain(out);
            collected.insert(collected.end(), got.begin(), got.end());
        }
        if (written == 0) {
            auto got = drain(out);
            collected.insert(collected.end(), got.begin(), got.end());
        }
    }
    while (block.procedure(&out).is_ok()) {
        auto got = drain(out);
        collected.insert(collected.end(), got.begin(), got.end());
    }
    auto tail = drain(out);
    collected.insert(collected.end(), tail.begin(), tail.end());
    return collected;
}

}  // namespace

TEST_F(ResamplerBlocksTest, RationalResamplerBlockExactCountAndBatchContinuity) {
    const size_t num_input = 60000;
    std::vector<std::complex<float>> input(num_input);
    for (size_t i = 0; i < num_input; ++i) {
        const float t = static_cast<float>(i);
        input[i] = std::complex<float>(std::sin(0.017f * t) + 0.3f * std::sin(0.211f * t),
                                       std::cos(0.017f * t) - 0.3f * std::cos(0.109f * t));
    }

    RatKernel reference(kRatAtten);
    std::vector<std::complex<float>> expected(RatKernel::max_outputs(num_input));
    const size_t expected_n = reference.process(input.data(), num_input, expected.data());
    expected.resize(expected_n);
    EXPECT_EQ(expected_n, num_input * kRatInterp / kRatDecim);

    for (size_t chunk : {509u, 4096u, 16384u}) {
        RatBlock block("rat_block", kRatAtten, 32768);
        cler::Channel<std::complex<float>> out(32768);
        const auto got = run_block_in_chunks(block, out, input, chunk);

        ASSERT_EQ(got.size(), expected.size()) << "chunk=" << chunk;
        for (size_t i = 0; i < got.size(); ++i) {
            ASSERT_NEAR(got[i].real(), expected[i].real(), 1e-5f) << "chunk=" << chunk << " i=" << i;
            ASSERT_NEAR(got[i].imag(), expected[i].imag(), 1e-5f) << "chunk=" << chunk << " i=" << i;
        }
    }
}

TEST_F(ResamplerBlocksTest, RationalResamplerBlockFrequencyResponse) {
    const float input_rate = 600e3f;
    const size_t num_input = 24000;

    auto rms_out_for_tone = [&](float tone_hz) {
        RatBlock block("rat_resp", kRatAtten, 32768);
        cler::Channel<std::complex<float>> out(32768);
        const auto input = generate_complex_exponential(num_input, tone_hz, input_rate);
        const auto got = run_block_in_chunks(block, out, input, 4096);
        EXPECT_GT(got.size(), num_input / 2);
        double acc = 0.0;
        const size_t skip = 64;
        for (size_t i = skip; i < got.size(); ++i) acc += std::norm(got[i]);
        return std::sqrt(acc / static_cast<double>(got.size() - skip));
    };

    const double dc_ref = rms_out_for_tone(0.0f);
    const double pass   = rms_out_for_tone(100e3f);
    const double stop   = rms_out_for_tone(300e3f);

    EXPECT_NEAR(20.0 * std::log10(pass / dc_ref), 0.0, 1.0);
    EXPECT_LT(20.0 * std::log10(stop / dc_ref), -12.0);
}
