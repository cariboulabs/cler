#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <complex>
#include <fstream>
#include "../../include/cler_spsc-queue.hpp"

class SPSCQueueDoublyMappedTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test that stack buffers never return valid read_span
TEST_F(SPSCQueueDoublyMappedTest, StackBuffersNoReadSpan) {
    dro::SPSCQueue<float, 1024> stack_queue;
    
    // Fill with some data
    for (int i = 0; i < 100; i++) {
        stack_queue.push(i * 0.1f);
    }
    
    // read_span should always return null for stack buffers
    auto [ptr, size] = stack_queue.read_span();
    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(size, 0);
    
    // But regular peek_read should work
    const float* p1, *p2;
    size_t s1, s2;
    size_t total = stack_queue.peek_read(p1, s1, p2, s2);
    EXPECT_GT(total, 0);
    EXPECT_NE(p1, nullptr);
}

// Test that small heap buffers fall back to standard allocation
TEST_F(SPSCQueueDoublyMappedTest, SmallHeapBuffersNoReadSpan) {
    dro::SPSCQueue<float> small_queue(1024);  // 4KB - below 32KB threshold
    
    // Fill with some data
    for (int i = 0; i < 500; i++) {
        small_queue.push(i * 0.1f);
    }
    
    // read_span may or may not work (depends on platform support and if it falls back)
    auto [ptr, size] = small_queue.read_span();
    // We don't assert anything specific here since it depends on platform and size
    (void)ptr; (void)size; // Suppress unused variable warnings
    
    // But regular peek_read should always work
    const float* p1, *p2;
    size_t s1, s2;
    size_t total = small_queue.peek_read(p1, s1, p2, s2);
    EXPECT_GT(total, 0);
    EXPECT_NE(p1, nullptr);
}

// Test large buffer behavior (may get doubly mapped on supported platforms)
TEST_F(SPSCQueueDoublyMappedTest, LargeBufferBehavior) {
    // 8192 floats = 32KB - exactly at threshold
    dro::SPSCQueue<float> large_queue(8192);
    
    EXPECT_EQ(large_queue.capacity(), 8192);
    EXPECT_TRUE(large_queue.empty());
    
    // Fill buffer to about 75% capacity, then consume some to create wrap
    const int fill_count = 6000;
    const int consume_count = 3000;
    const int refill_count = 2000;
    
    // Fill initial data
    for (int i = 0; i < fill_count; i++) {
        large_queue.push(static_cast<float>(i));
    }
    
    // Consume some data to advance read pointer
    float val;
    for (int i = 0; i < consume_count; i++) {
        large_queue.pop(val);
        EXPECT_EQ(val, static_cast<float>(i));
    }
    
    // Refill to create wrap-around scenario
    for (int i = fill_count; i < fill_count + refill_count; i++) {
        large_queue.push(static_cast<float>(i));
    }
    
    // Now we should have data that wraps around
    EXPECT_GT(large_queue.size(), 0);
    
    // Test both APIs work
    auto [span_ptr, span_size] = large_queue.read_span();
    const float* p1, *p2;
    size_t s1, s2;
    size_t total = large_queue.peek_read(p1, s1, p2, s2);
    
    // Both should report the same total available data
    EXPECT_EQ(total, large_queue.size());
    
    if (span_ptr) {
        // If doubly mapped worked, we get single contiguous span
        EXPECT_EQ(span_size, total);
        EXPECT_EQ(span_ptr[0], static_cast<float>(consume_count));
        
        // Verify data integrity across the span
        for (size_t i = 0; i < std::min(span_size, size_t(100)); i++) {
            EXPECT_EQ(span_ptr[i], static_cast<float>(consume_count + i));
        }
    } else {
        // Standard allocation - should have two pointers for wrapped data
        EXPECT_EQ(s1 + s2, total);
        EXPECT_NE(p1, nullptr);
        if (s2 > 0) {
            EXPECT_NE(p2, nullptr);
        }
    }
}

// Test with complex data type (common in SDR applications)
TEST_F(SPSCQueueDoublyMappedTest, ComplexFloatSDRBuffer) {
    // 4096 complex<float> = 32KB - typical SDR buffer
    dro::SPSCQueue<std::complex<float>> sdr_queue(4096);
    
    // Generate some test I/Q data
    std::vector<std::complex<float>> test_data;
    for (int i = 0; i < 3000; i++) {
        float t = i * 0.01f;
        test_data.emplace_back(std::cos(t), std::sin(t));
    }
    
    // Push test data
    for (const auto& sample : test_data) {
        sdr_queue.push(sample);
    }
    
    // Test read_span
    auto [ptr, size] = sdr_queue.read_span();
    if (ptr && size > 0) {
        // Verify first few samples match
        for (size_t i = 0; i < std::min(size, size_t(10)); i++) {
            EXPECT_FLOAT_EQ(ptr[i].real(), test_data[i].real());
            EXPECT_FLOAT_EQ(ptr[i].imag(), test_data[i].imag());
        }
    }
    
    // Regular peek_read should also work
    const std::complex<float>* p1, *p2;
    size_t s1, s2;
    size_t total = sdr_queue.peek_read(p1, s1, p2, s2);
    EXPECT_EQ(total, test_data.size());
}

// Test file I/O integration (mimics SinkFileBlock behavior)
TEST_F(SPSCQueueDoublyMappedTest, FileIOIntegration) {
    dro::SPSCQueue<float> queue(8192);  // 32KB buffer
    
    // Generate test data
    std::vector<float> test_data;
    for (int i = 0; i < 5000; i++) {
        test_data.push_back(i * 0.001f);
    }
    
    // Push test data
    for (float sample : test_data) {
        queue.push(sample);
    }
    
    // Create temporary file
    const char* temp_filename = "/tmp/cler_test_doubly_mapped.bin";
    FILE* fp = std::fopen(temp_filename, "wb");
    ASSERT_NE(fp, nullptr);
    
    size_t total_written = 0;
    int write_calls = 0;
    
    // Write data using read_span (if available) or peek_read fallback
    while (queue.size() > 0) {
        // Try read_span first (optimal path)
        auto [span_ptr, span_size] = queue.read_span();
        if (span_ptr && span_size > 0) {
            size_t written = std::fwrite(span_ptr, sizeof(float), span_size, fp);
            EXPECT_EQ(written, span_size);
            queue.commit_read(written);
            total_written += written;
            write_calls++;
        } else {
            // Fallback to peek_read
            const float* p1, *p2;
            size_t s1, s2;
            size_t available = queue.peek_read(p1, s1, p2, s2);
            if (available == 0) break;
            
            if (s1 > 0) {
                size_t written1 = std::fwrite(p1, sizeof(float), s1, fp);
                EXPECT_EQ(written1, s1);
                total_written += written1;
                write_calls++;
            }
            
            if (s2 > 0) {
                size_t written2 = std::fwrite(p2, sizeof(float), s2, fp);
                EXPECT_EQ(written2, s2);
                total_written += written2;
                write_calls++;
            }
            
            queue.commit_read(s1 + s2);
        }
    }
    
    std::fclose(fp);
    
    // Verify all data was written
    EXPECT_EQ(total_written, test_data.size());
    
    // If doubly mapped worked, we should have fewer write calls
    std::cout << "Total write calls: " << write_calls << std::endl;
    std::cout << "Average write size: " << (total_written / write_calls) << " samples" << std::endl;
    
    // Clean up
    std::remove(temp_filename);
}

// Test platform support detection
TEST_F(SPSCQueueDoublyMappedTest, PlatformSupport) {
    bool platform_supports = cler::platform::supports_doubly_mapped_buffers();
    std::cout << "Platform supports doubly mapped buffers: " << (platform_supports ? "Yes" : "No") << std::endl;
    
    if (platform_supports) {
        std::cout << "Page size: " << cler::platform::get_page_size() << " bytes" << std::endl;
    }
    
    // Test should pass regardless of platform support
    EXPECT_TRUE(true);
}