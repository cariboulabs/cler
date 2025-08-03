#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <cmath>
#include <numeric>

#include "cler.hpp"
#include "desktop_blocks/noise/awgn.hpp"

class NoiseBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    // Helper function to calculate sample mean
    template<typename T>
    double calculate_mean(const std::vector<T>& data) {
        if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
            // For complex, calculate mean of magnitudes
            double sum = 0.0;
            for (const auto& val : data) {
                sum += std::abs(val);
            }
            return sum / data.size();
        } else {
            // For real types
            double sum = std::accumulate(data.begin(), data.end(), 0.0);
            return sum / data.size();
        }
    }
    
    // Helper function to calculate sample standard deviation
    template<typename T>
    double calculate_stddev(const std::vector<T>& data) {
        if (data.empty()) return 0.0;
        
        double mean = calculate_mean(data);
        double sum_sq_diff = 0.0;
        
        if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
            // For complex, calculate stddev of magnitudes
            for (const auto& val : data) {
                double diff = std::abs(val) - mean;
                sum_sq_diff += diff * diff;
            }
        } else {
            // For real types
            for (const auto& val : data) {
                double diff = static_cast<double>(val) - mean;
                sum_sq_diff += diff * diff;
            }
        }
        
        return std::sqrt(sum_sq_diff / (data.size() - 1));
    }
};

// Test NoiseAWGNBlock with float - constant input signal
TEST_F(NoiseBlocksTest, AWGNBlockFloatZeroSignal) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 1.0f;
    const size_t num_samples = 2048; // Moderate sample for statistical testing
    const size_t chunk_size = buffer_size / 2; // Process in chunks to avoid buffer overflow
    const float signal_level = 1.5f; // Use non-zero signal to better detect issues
    
    NoiseAWGNBlock<float> noise_block("test_awgn_float", noise_stddev, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Process samples in chunks to avoid buffer overflow
    std::vector<float> output_data;
    size_t processed_samples = 0;
    
    while (processed_samples < num_samples) {
        size_t samples_this_chunk = std::min(chunk_size, num_samples - processed_samples);
        
        // Fill input with constant signal for this chunk
        for (size_t i = 0; i < samples_this_chunk; i++) {
            noise_block.in.push(signal_level);
        }
        
        // Process this chunk
        auto result = noise_block.procedure(&output);
        if (result.is_err()) {
            // Handle expected errors gracefully
            if (result.unwrap_err() == cler::Error::NotEnoughSpace) {
                // Output buffer full, need to drain it first
                float sample;
                while (output.try_pop(sample)) {
                    output_data.push_back(sample);
                }
                // Retry the procedure
                result = noise_block.procedure(&output);
            }
        }
        EXPECT_TRUE(result.is_ok());
        
        // Collect output samples
        float sample;
        while (output.try_pop(sample)) {
            output_data.push_back(sample);
        }
        
        processed_samples += samples_this_chunk;
    }
    
    // Verify we got all samples
    EXPECT_EQ(output_data.size(), num_samples);
    
    // Statistical tests on the output (signal + noise)
    double mean = calculate_mean(output_data);
    double stddev = calculate_stddev(output_data);
    
    // Mean should be close to signal_level (within 3 sigma / sqrt(N))
    double expected_mean_error = 3.0 * noise_stddev / std::sqrt(num_samples);
    EXPECT_NEAR(mean, signal_level, expected_mean_error) << "Output mean deviates too much from signal level";
    
    // Standard deviation should be close to noise stddev (within 10% for large N)
    EXPECT_GT(stddev, noise_stddev * 0.9) << "Output stddev too small";
    EXPECT_LT(stddev, noise_stddev * 1.1) << "Output stddev too large";
}

// Test NoiseAWGNBlock with float - non-zero input signal
TEST_F(NoiseBlocksTest, AWGNBlockFloatNonZeroSignal) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 0.5f;
    const float signal_level = 3.0f;
    
    NoiseAWGNBlock<float> noise_block("test_awgn_float_signal", noise_stddev, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill input with constant signal
    std::vector<float> input_data = {signal_level, signal_level, signal_level, signal_level, signal_level};
    
    for (float value : input_data) {
        noise_block.in.push(value);
    }
    
    // Process samples
    auto result = noise_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size
    EXPECT_EQ(output.size(), input_data.size());
    
    // Collect and verify output
    std::vector<float> output_data;
    for (size_t i = 0; i < input_data.size(); i++) {
        float sample;
        ASSERT_TRUE(output.try_pop(sample));
        output_data.push_back(sample);
        
        // Output should be signal + noise, so should be "close" to original signal
        // Within reasonable noise bounds (say 5 sigma)
        EXPECT_GT(sample, signal_level - 5.0f * noise_stddev) << "Output too far below signal at index " << i;
        EXPECT_LT(sample, signal_level + 5.0f * noise_stddev) << "Output too far above signal at index " << i;
    }
    
    // The noise added should have increased the variance
    double input_mean = calculate_mean(input_data);
    double output_mean = calculate_mean(output_data);
    double output_stddev = calculate_stddev(output_data);
    
    // Output mean should still be close to signal level
    // Using 2 * noise_stddev for more robust testing against random variations
    EXPECT_NEAR(output_mean, input_mean, noise_stddev * 2) << "Output mean shifted too much";
    
    // Output should have more variance than pure signal
    EXPECT_GT(output_stddev, 0.1) << "Output stddev should be non-zero due to added noise";
}

// Test NoiseAWGNBlock with complex<float> - constant input signal  
TEST_F(NoiseBlocksTest, AWGNBlockComplexZeroSignal) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 1.0f;
    const size_t num_samples = 2048; // Moderate sample for statistical testing
    const size_t chunk_size = buffer_size / 2; // Process in chunks to avoid buffer overflow
    const std::complex<float> signal_level{2.0f, 1.0f}; // Use non-zero signal to better detect issues
    
    NoiseAWGNBlock<std::complex<float>> noise_block("test_awgn_complex", noise_stddev, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Process samples in chunks to avoid buffer overflow
    std::vector<std::complex<float>> output_data;
    size_t processed_samples = 0;
    
    while (processed_samples < num_samples) {
        size_t samples_this_chunk = std::min(chunk_size, num_samples - processed_samples);
        
        // Fill input with constant complex signal for this chunk
        for (size_t i = 0; i < samples_this_chunk; i++) {
            noise_block.in.push(signal_level);
        }
        
        // Process this chunk
        auto result = noise_block.procedure(&output);
        if (result.is_err()) {
            // Handle expected errors gracefully
            if (result.unwrap_err() == cler::Error::NotEnoughSpace) {
                // Output buffer full, need to drain it first
                std::complex<float> sample;
                while (output.try_pop(sample)) {
                    output_data.push_back(sample);
                }
                // Retry the procedure
                result = noise_block.procedure(&output);
            }
        }
        EXPECT_TRUE(result.is_ok());
        
        // Collect output samples
        std::complex<float> sample;
        while (output.try_pop(sample)) {
            output_data.push_back(sample);
        }
        
        processed_samples += samples_this_chunk;
    }
    
    // Verify we got all samples
    EXPECT_EQ(output_data.size(), num_samples);
    
    // For complex noise, test real and imaginary parts separately
    std::vector<float> real_parts, imag_parts;
    for (const auto& sample : output_data) {
        real_parts.push_back(sample.real());
        imag_parts.push_back(sample.imag());
    }
    
    // Both real and imaginary parts should be normally distributed with specified stddev
    double real_mean = calculate_mean(real_parts);
    double real_stddev = calculate_stddev(real_parts);
    double imag_mean = calculate_mean(imag_parts);
    double imag_stddev = calculate_stddev(imag_parts);
    
    // Means should be close to signal_level components
    double expected_mean_error = 3.0 * noise_stddev / std::sqrt(num_samples);
    EXPECT_NEAR(real_mean, signal_level.real(), expected_mean_error) << "Real part mean deviates too much";
    EXPECT_NEAR(imag_mean, signal_level.imag(), expected_mean_error) << "Imag part mean deviates too much";
    
    // Standard deviations should be close to specified value
    EXPECT_GT(real_stddev, noise_stddev * 0.9) << "Real part stddev too small";
    EXPECT_LT(real_stddev, noise_stddev * 1.1) << "Real part stddev too large";
    EXPECT_GT(imag_stddev, noise_stddev * 0.9) << "Imag part stddev too small";
    EXPECT_LT(imag_stddev, noise_stddev * 1.1) << "Imag part stddev too large";
}

// Test NoiseAWGNBlock with complex<float> - non-zero input signal
TEST_F(NoiseBlocksTest, AWGNBlockComplexNonZeroSignal) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 0.2f;
    
    NoiseAWGNBlock<std::complex<float>> noise_block("test_awgn_complex_signal", noise_stddev, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Fill input with complex signal
    std::vector<std::complex<float>> input_data = {
        {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}, {1.0f, 1.0f}
    };
    
    for (const auto& value : input_data) {
        noise_block.in.push(value);
    }
    
    // Process samples
    auto result = noise_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size
    EXPECT_EQ(output.size(), input_data.size());
    
    // Collect and verify output
    for (size_t i = 0; i < input_data.size(); i++) {
        std::complex<float> sample;
        ASSERT_TRUE(output.try_pop(sample));
        
        // Output should be close to input (within noise bounds)
        // Check real and imaginary parts separately
        float real_diff = std::abs(sample.real() - input_data[i].real());
        float imag_diff = std::abs(sample.imag() - input_data[i].imag());
        
        EXPECT_LT(real_diff, 5.0f * noise_stddev) << "Real part too far from input at index " << i;
        EXPECT_LT(imag_diff, 5.0f * noise_stddev) << "Imag part too far from input at index " << i;
    }
}

// Test NoiseAWGNBlock with double precision
TEST_F(NoiseBlocksTest, AWGNBlockDouble) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const double noise_stddev = 0.1;
    
    NoiseAWGNBlock<double> noise_block("test_awgn_double", noise_stddev, buffer_size);
    cler::Channel<double> output(buffer_size);
    
    // Fill input with test data
    std::vector<double> input_data = {1.0, -2.5, 3.14159, 0.0, 42.0};
    
    for (double value : input_data) {
        noise_block.in.push(value);
    }
    
    // Process samples
    auto result = noise_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size
    EXPECT_EQ(output.size(), input_data.size());
    
    // Collect and verify output
    for (size_t i = 0; i < input_data.size(); i++) {
        double sample;
        ASSERT_TRUE(output.try_pop(sample));
        
        // Output should be close to input (within noise bounds)
        double diff = std::abs(sample - input_data[i]);
        EXPECT_LT(diff, 5.0 * noise_stddev) << "Output too far from input at index " << i;
    }
}

// Test NoiseAWGNBlock with zero noise (should be passthrough)
TEST_F(NoiseBlocksTest, AWGNBlockZeroNoise) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 0.0f; // Zero noise
    
    NoiseAWGNBlock<float> noise_block("test_awgn_zero_noise", noise_stddev, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill input with test data
    std::vector<float> input_data = {1.5f, -2.7f, 3.14f, 0.0f, 99.9f};
    
    for (float value : input_data) {
        noise_block.in.push(value);
    }
    
    // Process samples
    auto result = noise_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size
    EXPECT_EQ(output.size(), input_data.size());
    
    // With zero noise, output should exactly match input
    for (size_t i = 0; i < input_data.size(); i++) {
        float sample;
        ASSERT_TRUE(output.try_pop(sample));
        EXPECT_FLOAT_EQ(sample, input_data[i]) << "Zero noise should preserve input exactly at index " << i;
    }
}

// Test NoiseAWGNBlock randomness - multiple runs should produce different results
TEST_F(NoiseBlocksTest, AWGNBlockRandomness) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const float noise_stddev = 1.0f;
    
    // Create two identical blocks
    NoiseAWGNBlock<float> noise_block1("test_awgn_random1", noise_stddev, buffer_size);
    NoiseAWGNBlock<float> noise_block2("test_awgn_random2", noise_stddev, buffer_size);
    cler::Channel<float> output1(buffer_size);
    cler::Channel<float> output2(buffer_size);
    
    // Fill both with identical zero input
    for (int i = 0; i < 100; i++) {
        noise_block1.in.push(0.0f);
        noise_block2.in.push(0.0f);
    }
    
    // Process samples
    auto result1 = noise_block1.procedure(&output1);
    auto result2 = noise_block2.procedure(&output2);
    EXPECT_TRUE(result1.is_ok());
    EXPECT_TRUE(result2.is_ok());
    
    // Collect outputs
    std::vector<float> samples1, samples2;
    float sample;
    while (output1.try_pop(sample)) {
        samples1.push_back(sample);
    }
    while (output2.try_pop(sample)) {
        samples2.push_back(sample);
    }
    
    // Both should have same size
    EXPECT_EQ(samples1.size(), samples2.size());
    
    // But the values should be different (with very high probability)
    int differences = 0;
    for (size_t i = 0; i < std::min(samples1.size(), samples2.size()); i++) {
        if (std::abs(samples1[i] - samples2[i]) > 1e-6) {
            differences++;
        }
    }
    
    // We expect most samples to be different due to randomness
    EXPECT_GT(differences, samples1.size() * 0.9) << "Noise should be random - too many identical samples";
}