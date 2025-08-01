#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <fstream>
#include <cstdio>
#include <cstring>

#include "cler.hpp"
#include "sinks/sink_null.hpp"
#include "sinks/sink_file.hpp"

class SinkBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique test filename
        test_filename = "/tmp/cler_test_sink_" + std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".bin";
    }
    
    void TearDown() override {
        // Clean up test file if it exists
        std::remove(test_filename.c_str());
    }
    
    std::string test_filename;
};

// Test SinkNullBlock without callback - should consume all data
TEST_F(SinkBlocksTest, SinkNullBlockNoCallback) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    SinkNullBlock<float> sink_block("test_sink_null", nullptr, nullptr, buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, -2.5f, 3.14f, 0.0f, 99.9f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    // Verify input has data
    EXPECT_EQ(sink_block.in.size(), test_data.size());
    
    // Run the block
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Verify all data was consumed
    EXPECT_EQ(sink_block.in.size(), 0);
}

// Test SinkNullBlock with callback - verify callback functionality
TEST_F(SinkBlocksTest, SinkNullBlockWithCallback) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Callback data to track what was received
    struct CallbackData {
        std::vector<float> received_data;
        size_t call_count = 0;
    } callback_data;
    
    // Define callback that collects data and returns how much to consume
    auto callback = [](cler::Channel<float>* channel, void* context) -> size_t {
        auto* data = static_cast<CallbackData*>(context);
        data->call_count++;
        
        // Read all available data using try_pop (this advances the read pointer)
        size_t consumed = 0;
        float sample;
        while (channel->try_pop(sample)) {
            data->received_data.push_back(sample);
            consumed++;
        }
        
        // Return 0 since we already consumed the data with try_pop
        // The SinkNullBlock shouldn't call commit_read again
        return 0;
    };
    
    SinkNullBlock<float> sink_block("test_sink_null_callback", callback, &callback_data, buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.1f, 2.2f, -3.3f, 4.4f, 0.0f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    // Run the block
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Verify callback was called
    EXPECT_EQ(callback_data.call_count, 1);
    
    // Verify callback received correct data
    EXPECT_EQ(callback_data.received_data.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(callback_data.received_data[i], test_data[i]) << "Callback data mismatch at index " << i;
    }
    
    // After commit_read(), the channel should be empty since all data was consumed
    EXPECT_EQ(sink_block.in.size(), 0);
}

// Test SinkNullBlock with partial consumption callback
TEST_F(SinkBlocksTest, SinkNullBlockPartialCallback) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Callback that tells the block to consume only half the data
    auto partial_callback = [](cler::Channel<float>* channel, void* context) -> size_t {
        size_t available = channel->size();
        size_t to_consume = available / 2; // Only consume half
        
        // Don't actually pop data here - let commit_read do it
        // Just return how many samples the block should consume
        return to_consume;
    };
    
    SinkNullBlock<float> sink_block("test_sink_null_partial", partial_callback, nullptr, buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    // Run the block
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Verify only half the data was consumed
    size_t expected_remaining = test_data.size() - (test_data.size() / 2);
    EXPECT_EQ(sink_block.in.size(), expected_remaining);
}

// Test SinkNullBlock error conditions
TEST_F(SinkBlocksTest, SinkNullBlockErrorConditions) {
    // Test zero buffer size - may throw invalid_argument or logic_error depending on implementation
    EXPECT_THROW(SinkNullBlock<float>("test", nullptr, nullptr, 0), std::exception);
}

// Test SinkFileBlock basic functionality
TEST_F(SinkBlocksTest, SinkFileBlockBasic) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    SinkFileBlock<float> sink_block("test_sink_file", test_filename.c_str(), buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.5f, -2.7f, 3.14159f, 0.0f, -99.99f, 42.0f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    // Run the block
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Verify all data was consumed
    EXPECT_EQ(sink_block.in.size(), 0);
    
    // Manually flush and close to ensure data is written
    // (destructor will do this, but we want to verify before reading)
}

// Test SinkFileBlock data verification - read back and compare
TEST_F(SinkBlocksTest, SinkFileBlockDataVerification) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Test data
    std::vector<float> test_data = {
        1.0f, -2.5f, 3.14159265f, 0.0f, 999.999f, -0.001f, 42.42f
    };
    
    // Write data using SinkFileBlock
    {
        SinkFileBlock<float> sink_block("test_sink_file_verify", test_filename.c_str(), buffer_size);
        
        for (float value : test_data) {
            sink_block.in.push(value);
        }
        
        auto result = sink_block.procedure();
        EXPECT_TRUE(result.is_ok());
        
        // Block destructor will close file
    }
    
    // Read back and verify
    std::ifstream file(test_filename, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "Failed to open file for reading";
    
    std::vector<float> read_data;
    float value;
    while (file.read(reinterpret_cast<char*>(&value), sizeof(float))) {
        read_data.push_back(value);
    }
    file.close();
    
    // Verify data matches
    EXPECT_EQ(read_data.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(read_data[i], test_data[i]) << "File data mismatch at index " << i;
    }
}

// Test SinkFileBlock with complex data
TEST_F(SinkBlocksTest, SinkFileBlockComplex) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, -1.0f}, {2.5f, 3.5f}, {0.0f, 0.0f}, {-7.2f, 8.1f}, {99.9f, -99.9f}
    };
    
    // Write data using SinkFileBlock
    {
        SinkFileBlock<std::complex<float>> sink_block("test_sink_file_complex", test_filename.c_str(), buffer_size);
        
        for (const auto& value : test_data) {
            sink_block.in.push(value);
        }
        
        auto result = sink_block.procedure();
        EXPECT_TRUE(result.is_ok());
        
        // Block destructor will close file
    }
    
    // Read back and verify
    std::ifstream file(test_filename, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "Failed to open file for reading";
    
    std::vector<std::complex<float>> read_data;
    std::complex<float> value;
    while (file.read(reinterpret_cast<char*>(&value), sizeof(std::complex<float>))) {
        read_data.push_back(value);
    }
    file.close();
    
    // Verify data matches
    EXPECT_EQ(read_data.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(read_data[i].real(), test_data[i].real()) << "File real mismatch at index " << i;
        EXPECT_FLOAT_EQ(read_data[i].imag(), test_data[i].imag()) << "File imag mismatch at index " << i;
    }
}

// Test SinkFileBlock multiple runs - verify data accumulation
TEST_F(SinkBlocksTest, SinkFileBlockMultipleRuns) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Combine expected data
    std::vector<int> batch1 = {10, 20, 30};
    std::vector<int> batch2 = {40, 50, 60, 70};
    std::vector<int> expected_data = batch1;
    expected_data.insert(expected_data.end(), batch2.begin(), batch2.end());
    
    // Write data using SinkFileBlock in a separate scope
    {
        SinkFileBlock<int> sink_block("test_sink_file_multiple", test_filename.c_str(), buffer_size);
        
        // First batch
        for (int value : batch1) {
            sink_block.in.push(value);
        }
        
        auto result1 = sink_block.procedure();
        EXPECT_TRUE(result1.is_ok());
        
        // Second batch
        for (int value : batch2) {
            sink_block.in.push(value);
        }
        
        auto result2 = sink_block.procedure();
        EXPECT_TRUE(result2.is_ok());
        
        // SinkFileBlock destructor runs here automatically, closing file
    }
    
    // Read back and verify all data is there
    std::ifstream file(test_filename, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "Failed to open file for reading";
    
    std::vector<int> read_data;
    int value;
    while (file.read(reinterpret_cast<char*>(&value), sizeof(int))) {
        read_data.push_back(value);
    }
    file.close();
    
    // Verify data matches
    EXPECT_EQ(read_data.size(), expected_data.size());
    for (size_t i = 0; i < expected_data.size(); i++) {
        EXPECT_EQ(read_data[i], expected_data[i]) << "Multiple run data mismatch at index " << i;
    }
}

// Test SinkFileBlock error conditions
TEST_F(SinkBlocksTest, SinkFileBlockErrorConditions) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Test zero buffer size - Channel throws std::logic_error for zero capacity
    EXPECT_THROW(SinkFileBlock<float>("test", test_filename.c_str(), 0), std::logic_error);
    
    // Test empty filename
    EXPECT_THROW(SinkFileBlock<float>("test", "", buffer_size), std::invalid_argument);
    EXPECT_THROW(SinkFileBlock<float>("test", nullptr, buffer_size), std::invalid_argument);
    
    // Test invalid file path (assuming /invalid/path doesn't exist)
    EXPECT_THROW(SinkFileBlock<float>("test", "/invalid/path/file.bin", buffer_size), std::runtime_error);
}

// Test SinkFileBlock with empty input
TEST_F(SinkBlocksTest, SinkFileBlockEmptyInput) {
    const size_t buffer_size = 4096; // Large enough for dbf
    
    // Write empty data using SinkFileBlock in a separate scope
    {
        SinkFileBlock<float> sink_block("test_sink_file_empty", test_filename.c_str(), buffer_size);
        
        // Run with empty input
        auto result = sink_block.procedure();
        EXPECT_TRUE(result.is_ok());
        
        // Verify input is still empty
        EXPECT_EQ(sink_block.in.size(), 0);
        
        // SinkFileBlock destructor runs here automatically, closing file
    }
    
    // Verify file is empty
    std::ifstream file(test_filename, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.is_open()) << "Failed to open file for reading";
    
    // File should be empty (size 0)
    EXPECT_EQ(file.tellg(), 0);
    file.close();
}