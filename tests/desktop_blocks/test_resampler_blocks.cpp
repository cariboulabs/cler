#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <cmath>

#include "cler.hpp"
#include "resamplers/multistage_resampler.hpp"

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
TEST_F(ResamplerBlocksTest, MultiStageResamplerErrorConditions) {
    const size_t buffer_size = 4096;
    const float attenuation = 60.0f;
    
    // Test buffer size too small for doubly-mapped buffers (need at least 4096/sizeof(float) = 1024 for float)
    EXPECT_THROW(MultiStageResamplerBlock<float>("test", 2.0f, attenuation, 1), std::invalid_argument);
    
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
    
    // This should throw std::invalid_argument because buffer is too small
    EXPECT_THROW(MultiStageResamplerBlock<float>("test_resampler_small", ratio, attenuation, small_buffer), std::invalid_argument);
}