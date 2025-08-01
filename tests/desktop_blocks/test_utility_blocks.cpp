#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <chrono>
#include <thread>

#include "cler.hpp"
#include "utils/fanout.hpp"
#include "utils/throttle.hpp"
#include "utils/throughput.hpp"

class UtilityBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test FanoutBlock with 2 outputs - verify identical copies
TEST_F(UtilityBlocksTest, FanoutBlock2Outputs) {
    const size_t buffer_size = 1024;
    const size_t num_outputs = 2;
    
    FanoutBlock<float> fanout_block("test_fanout", num_outputs, buffer_size);
    cler::Channel<float> output1(buffer_size);
    cler::Channel<float> output2(buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, -2.5f, 3.14f, 0.0f, -99.9f, 42.0f};
    
    for (float value : test_data) {
        fanout_block.in.push(value);
    }
    
    // Run the block
    auto result = fanout_block.procedure(&output1, &output2);
    EXPECT_TRUE(result.is_ok());
    
    // Verify both outputs have same size as input
    EXPECT_EQ(output1.size(), test_data.size());
    EXPECT_EQ(output2.size(), test_data.size());
    
    // Verify both outputs contain identical copies of input data
    for (size_t i = 0; i < test_data.size(); i++) {
        float actual1, actual2;
        ASSERT_TRUE(output1.try_pop(actual1));
        ASSERT_TRUE(output2.try_pop(actual2));
        
        EXPECT_FLOAT_EQ(actual1, test_data[i]) << "Output1 mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual2, test_data[i]) << "Output2 mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual1, actual2) << "Outputs don't match each other at index " << i;
    }
}

// Test FanoutBlock with 4 outputs - complex data
TEST_F(UtilityBlocksTest, FanoutBlock4OutputsComplex) {
    const size_t buffer_size = 1024;
    const size_t num_outputs = 4;
    
    FanoutBlock<std::complex<float>> fanout_block("test_fanout_complex", num_outputs, buffer_size);
    cler::Channel<std::complex<float>> output1(buffer_size);
    cler::Channel<std::complex<float>> output2(buffer_size);
    cler::Channel<std::complex<float>> output3(buffer_size);
    cler::Channel<std::complex<float>> output4(buffer_size);
    
    // Fill input with test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, 2.0f}, {-3.0f, 4.0f}, {0.0f, -1.0f}, {5.5f, 0.0f}
    };
    
    for (const auto& value : test_data) {
        fanout_block.in.push(value);
    }
    
    // Run the block
    auto result = fanout_block.procedure(&output1, &output2, &output3, &output4);
    EXPECT_TRUE(result.is_ok());
    
    // Verify all outputs have same size as input
    EXPECT_EQ(output1.size(), test_data.size());
    EXPECT_EQ(output2.size(), test_data.size());
    EXPECT_EQ(output3.size(), test_data.size());
    EXPECT_EQ(output4.size(), test_data.size());
    
    // Verify all outputs contain identical copies of input data
    for (size_t i = 0; i < test_data.size(); i++) {
        std::complex<float> actual1, actual2, actual3, actual4;
        ASSERT_TRUE(output1.try_pop(actual1));
        ASSERT_TRUE(output2.try_pop(actual2));
        ASSERT_TRUE(output3.try_pop(actual3));
        ASSERT_TRUE(output4.try_pop(actual4));
        
        EXPECT_FLOAT_EQ(actual1.real(), test_data[i].real()) << "Output1 real mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual1.imag(), test_data[i].imag()) << "Output1 imag mismatch at index " << i;
        
        // Verify all outputs are identical
        EXPECT_EQ(actual1, actual2) << "Output1 vs Output2 mismatch at index " << i;
        EXPECT_EQ(actual1, actual3) << "Output1 vs Output3 mismatch at index " << i;
        EXPECT_EQ(actual1, actual4) << "Output1 vs Output4 mismatch at index " << i;
    }
}

// Test FanoutBlock with empty input
TEST_F(UtilityBlocksTest, FanoutBlockEmptyInput) {
    const size_t buffer_size = 1024;
    const size_t num_outputs = 2;
    
    FanoutBlock<float> fanout_block("test_fanout_empty", num_outputs, buffer_size);
    cler::Channel<float> output1(buffer_size);
    cler::Channel<float> output2(buffer_size);
    
    // Run the block with empty input
    auto result = fanout_block.procedure(&output1, &output2);
    EXPECT_TRUE(result.is_ok());
    
    // Verify outputs are empty
    EXPECT_EQ(output1.size(), 0);
    EXPECT_EQ(output2.size(), 0);
}

// Test ThrottleBlock - verify timing and sample order
TEST_F(UtilityBlocksTest, ThrottleBlockTiming) {
    const size_t buffer_size = 1024;
    const size_t sps = 100; // 100 samples per second -> 10ms per sample
    
    ThrottleBlock<float> throttle_block("test_throttle", sps, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    
    for (float value : test_data) {
        throttle_block.in.push(value);
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Process samples one by one
    std::vector<float> output_data;
    for (size_t i = 0; i < test_data.size(); i++) {
        auto result = throttle_block.procedure(&output);
        EXPECT_TRUE(result.is_ok());
        
        float sample;
        ASSERT_TRUE(output.try_pop(sample));
        output_data.push_back(sample);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Verify sample order is preserved
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(output_data[i], test_data[i]) << "Sample order mismatch at index " << i;
    }
    
    // Verify timing - should take approximately (samples-1) * (1/sps) seconds
    // We expect about 40ms for 5 samples at 100 SPS (4 intervals of 10ms each)
    // Allow some tolerance for timing variations
    int64_t expected_min_ms = (test_data.size() - 1) * (1000 / sps) - 5; // -5ms tolerance
    int64_t expected_max_ms = (test_data.size() - 1) * (1000 / sps) + 15; // +15ms tolerance
    
    EXPECT_GE(elapsed.count(), expected_min_ms) << "Throttle too fast";
    EXPECT_LE(elapsed.count(), expected_max_ms) << "Throttle too slow";
}

// Test ThrottleBlock error conditions
TEST_F(UtilityBlocksTest, ThrottleBlockErrorConditions) {
    const size_t buffer_size = 1024;
    
    // Test zero sample rate
    EXPECT_THROW(ThrottleBlock<float>("test", 0, buffer_size), std::invalid_argument);
    
    // Test zero buffer size - may throw invalid_argument or logic_error depending on implementation
    EXPECT_THROW(ThrottleBlock<float>("test", 1000, 0), std::exception);
}

// Test ThrottleBlock with empty input
TEST_F(UtilityBlocksTest, ThrottleBlockEmptyInput) {
    const size_t buffer_size = 1024;
    const size_t sps = 1000;
    
    ThrottleBlock<float> throttle_block("test_throttle_empty", sps, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Run with empty input
    auto result = throttle_block.procedure(&output);
    EXPECT_FALSE(result.is_ok()); // Should return NotEnoughSamples error
    EXPECT_EQ(output.size(), 0);
}

// Test ThroughputBlock - verify passthrough and counting
TEST_F(UtilityBlocksTest, ThroughputBlockPassthrough) {
    const size_t buffer_size = 1024;
    
    ThroughputBlock<float> throughput_block("test_throughput", buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.1f, -2.2f, 3.3f, -4.4f, 5.5f, 0.0f, 99.9f};
    
    for (float value : test_data) {
        throughput_block.in.push(value);
    }
    
    // Run the block
    auto result = throughput_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size matches input
    EXPECT_EQ(output.size(), test_data.size());
    
    // Verify sample counting
    EXPECT_EQ(throughput_block.samples_passed(), test_data.size());
    
    // Verify data passthrough - exact same values in same order
    for (size_t i = 0; i < test_data.size(); i++) {
        float actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual, test_data[i]) << "Passthrough mismatch at index " << i;
    }
}

// Test ThroughputBlock with complex data
TEST_F(UtilityBlocksTest, ThroughputBlockComplex) {
    const size_t buffer_size = 1024;
    
    ThroughputBlock<std::complex<float>> throughput_block("test_throughput_complex", buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Fill input with test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, -1.0f}, {2.5f, 3.5f}, {0.0f, 0.0f}, {-7.2f, 8.1f}
    };
    
    for (const auto& value : test_data) {
        throughput_block.in.push(value);
    }
    
    // Run the block
    auto result = throughput_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output size matches input
    EXPECT_EQ(output.size(), test_data.size());
    
    // Verify sample counting
    EXPECT_EQ(throughput_block.samples_passed(), test_data.size());
    
    // Verify data passthrough - exact same values in same order
    for (size_t i = 0; i < test_data.size(); i++) {
        std::complex<float> actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual.real(), test_data[i].real()) << "Real passthrough mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual.imag(), test_data[i].imag()) << "Imag passthrough mismatch at index " << i;
    }
}

// Test ThroughputBlock multiple runs - verify cumulative counting
TEST_F(UtilityBlocksTest, ThroughputBlockCumulativeCounting) {
    const size_t buffer_size = 1024;
    
    ThroughputBlock<int> throughput_block("test_throughput_cumulative", buffer_size);
    cler::Channel<int> output(buffer_size);
    
    // First batch
    std::vector<int> batch1 = {10, 20, 30};
    for (int value : batch1) {
        throughput_block.in.push(value);
    }
    
    auto result1 = throughput_block.procedure(&output);
    EXPECT_TRUE(result1.is_ok());
    EXPECT_EQ(throughput_block.samples_passed(), batch1.size());
    
    // Clear output
    int dummy;
    while (output.try_pop(dummy)) {}
    
    // Second batch
    std::vector<int> batch2 = {40, 50};
    for (int value : batch2) {
        throughput_block.in.push(value);
    }
    
    auto result2 = throughput_block.procedure(&output);
    EXPECT_TRUE(result2.is_ok());
    EXPECT_EQ(throughput_block.samples_passed(), batch1.size() + batch2.size());
    
    // Verify second batch output
    for (size_t i = 0; i < batch2.size(); i++) {
        int actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_EQ(actual, batch2[i]) << "Second batch mismatch at index " << i;
    }
}

// Test ThroughputBlock with empty input
TEST_F(UtilityBlocksTest, ThroughputBlockEmptyInput) {
    const size_t buffer_size = 1024;
    
    ThroughputBlock<float> throughput_block("test_throughput_empty", buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Run with empty input
    auto result = throughput_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify no samples passed
    EXPECT_EQ(throughput_block.samples_passed(), 0);
    EXPECT_EQ(output.size(), 0);
}

// Test ThroughputBlock error conditions
TEST_F(UtilityBlocksTest, ThroughputBlockErrorConditions) {
    // Test zero buffer size - may throw invalid_argument or logic_error depending on implementation
    EXPECT_THROW(ThroughputBlock<float>("test", 0), std::exception);
}