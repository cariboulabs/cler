#include <gtest/gtest.h>
#include <vector>
#include <complex>
#include <fstream>
#include <filesystem>

#include "cler.hpp"
#include "sinks/sink_file.hpp"
#include "sinks/sink_null.hpp"

class SimpleSinkBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test SinkNullBlock basic functionality - no callback
TEST_F(SimpleSinkBlocksTest, SinkNullBlockBasic) {
    const size_t buffer_size = 1024;
    
    SinkNullBlock<float> sink_block("test_sink_null_basic", nullptr, nullptr, buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    EXPECT_EQ(sink_block.in.size(), test_data.size());
    
    // Run the block - should consume all data (no callback means consume all)
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Data should be consumed (committed)
    EXPECT_EQ(sink_block.in.size(), 0);
}

// Test SinkNullBlock with simple callback that returns all available
TEST_F(SimpleSinkBlocksTest, SinkNullBlockSimpleCallback) {
    const size_t buffer_size = 1024;
    
    size_t callback_calls = 0;
    auto callback = [](cler::Channel<float>* channel, void* context) -> size_t {
        size_t* call_count = static_cast<size_t*>(context);
        (*call_count)++;
        return channel->size(); // Commit all available data
    };
    
    SinkNullBlock<float> sink_block("test_sink_null_callback", callback, &callback_calls, buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, 2.0f, 3.0f};
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    EXPECT_EQ(sink_block.in.size(), test_data.size());
    
    // Run the block
    auto result = sink_block.procedure();
    EXPECT_TRUE(result.is_ok());
    
    // Verify callback was called
    EXPECT_EQ(callback_calls, 1);
    
    // Data should be consumed
    EXPECT_EQ(sink_block.in.size(), 0);
}

// Test SinkNullBlock error conditions
TEST_F(SimpleSinkBlocksTest, SinkNullBlockErrors) {
    // Test zero buffer size
    EXPECT_THROW(SinkNullBlock<float>("test", nullptr, nullptr, 0), std::exception);
}

// Test SinkFileBlock basic functionality
TEST_F(SimpleSinkBlocksTest, SinkFileBlockBasic) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const std::string filename = "/tmp/test_sink_file_basic.bin";
    
    // Clean up any existing file
    std::filesystem::remove(filename);
    
    // Fill input with test data
    std::vector<float> test_data = {1.1f, 2.2f, -3.3f, 4.4f, 0.0f};
    
    // Use a scope to ensure destructor runs before we check the file
    {
        SinkFileBlock<float> sink_block("test_sink_file", filename.c_str(), buffer_size);
    
    for (float value : test_data) {
        sink_block.in.push(value);
    }
    
    // Debug: Check what read_dbf returns
    auto [debug_ptr, debug_size] = sink_block.in.read_dbf();
    std::cout << "Before procedure - read_dbf ptr: " << debug_ptr << ", size: " << debug_size << std::endl;
    if (debug_size > 0) {
        std::cout << "First sample: " << debug_ptr[0] << std::endl;
    }
    
    // Run the block - wrap in try-catch to debug any exceptions
    try {
        auto result = sink_block.procedure();
        EXPECT_TRUE(result.is_ok()) << "SinkFileBlock procedure failed";
        
        // Verify data was consumed
        EXPECT_EQ(sink_block.in.size(), 0);
    } catch (const std::exception& e) {
        FAIL() << "SinkFileBlock procedure threw exception: " << e.what();
    }
    
    } // End scope to force destructor and flush file
    
    // Verify file was created and has correct size
    EXPECT_TRUE(std::filesystem::exists(filename));
    auto file_size = std::filesystem::file_size(filename);
    EXPECT_EQ(file_size, test_data.size() * sizeof(float));
    
    // Verify file contents
    std::ifstream file(filename, std::ios::binary);
    std::vector<float> read_data(test_data.size());
    file.read(reinterpret_cast<char*>(read_data.data()), file_size);
    file.close();
    
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(read_data[i], test_data[i]) << "File data mismatch at index " << i;
    }
    
    // Clean up
    std::filesystem::remove(filename);
}

// Test SinkFileBlock with complex data
TEST_F(SimpleSinkBlocksTest, SinkFileBlockComplex) {
    const size_t buffer_size = 4096; // Large enough for dbf
    const std::string filename = "/tmp/test_sink_file_complex.bin";
    
    // Clean up any existing file
    std::filesystem::remove(filename);
    
    // Fill input with complex test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, 2.0f}, {-3.0f, 4.0f}, {0.0f, -1.0f}
    };
    
    // Use a scope to ensure destructor runs before we check the file
    {
        SinkFileBlock<std::complex<float>> sink_block("test_sink_file_complex", filename.c_str(), buffer_size);
    
    for (const auto& value : test_data) {
        sink_block.in.push(value);
    }
    
    // Run the block - wrap in try-catch to debug any exceptions
    try {
        auto result = sink_block.procedure();
        EXPECT_TRUE(result.is_ok()) << "SinkFileBlock procedure failed";
        
        // Verify data was consumed
        EXPECT_EQ(sink_block.in.size(), 0);
    } catch (const std::exception& e) {
        FAIL() << "SinkFileBlock procedure threw exception: " << e.what();
    }
    
    } // End scope to force destructor and flush file
    
    // Verify file was created and has correct size
    EXPECT_TRUE(std::filesystem::exists(filename));
    auto file_size = std::filesystem::file_size(filename);
    EXPECT_EQ(file_size, test_data.size() * sizeof(std::complex<float>));
    
    // Verify file contents
    std::ifstream file(filename, std::ios::binary);
    std::vector<std::complex<float>> read_data(test_data.size());
    file.read(reinterpret_cast<char*>(read_data.data()), file_size);
    file.close();
    
    for (size_t i = 0; i < test_data.size(); i++) {
        EXPECT_FLOAT_EQ(read_data[i].real(), test_data[i].real()) << "Complex real mismatch at index " << i;
        EXPECT_FLOAT_EQ(read_data[i].imag(), test_data[i].imag()) << "Complex imag mismatch at index " << i;
    }
    
    // Clean up
    std::filesystem::remove(filename);
}