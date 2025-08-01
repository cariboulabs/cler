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

// Test that stack buffers throw exception for read_dbf
TEST_F(SPSCQueueDoublyMappedTest, StackBuffersThrowException) {
    dro::SPSCQueue<float, 1024> stack_queue;
    
    // Fill with some data
    for (int i = 0; i < 100; i++) {
        stack_queue.push(i * 0.1f);
    }
    
    // read_dbf should throw for stack buffers
    EXPECT_THROW(stack_queue.read_dbf(), std::runtime_error);
    EXPECT_THROW(stack_queue.write_dbf(), std::runtime_error);
    
    // But regular peek_read should work
    const float* p1, *p2;
    size_t s1, s2;
    size_t total = stack_queue.peek_read(p1, s1, p2, s2);
    EXPECT_GT(total, 0);
    EXPECT_NE(p1, nullptr);
}

// Test that small heap buffers throw exception when dbf not available
TEST_F(SPSCQueueDoublyMappedTest, SmallHeapBuffersThrowException) {
    dro::SPSCQueue<float> small_queue(512);  // 2KB - below 4KB threshold
    
    // Fill with some data
    for (int i = 0; i < 100; i++) {
        small_queue.push(i * 0.1f);
    }
    
    // read_dbf should throw for buffers below threshold
    EXPECT_THROW(small_queue.read_dbf(), std::runtime_error);
    EXPECT_THROW(small_queue.write_dbf(), std::runtime_error);
    
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
    auto [span_ptr, span_size] = large_queue.read_dbf();
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

// Comprehensive test to verify no samples are lost with read_dbf/write_dbf
TEST_F(SPSCQueueDoublyMappedTest, AllSamplesTransferredWithDbf) {
    // Create two queues large enough to use doubly-mapped buffers
    dro::SPSCQueue<float> source_queue(16384);  // 64KB
    dro::SPSCQueue<float> dest_queue(16384);    // 64KB
    
    // Generate test data with sequential values for easy verification
    const size_t total_samples = 50000;
    std::vector<float> test_data;
    test_data.reserve(total_samples);
    for (size_t i = 0; i < total_samples; i++) {
        test_data.push_back(static_cast<float>(i));
    }
    
    // Push data in chunks to create wrapping scenarios
    size_t pushed = 0;
    size_t transferred = 0;
    size_t chunk_size = 7500;  // Odd size to create interesting wrap patterns
    
    // Producer thread
    std::thread producer([&]() {
        while (pushed < total_samples) {
            size_t to_push = std::min(chunk_size, total_samples - pushed);
            size_t pushed_this_round = 0;
            
            while (pushed_this_round < to_push && pushed < total_samples) {
                if (source_queue.try_push(test_data[pushed])) {
                    pushed++;
                    pushed_this_round++;
                } else {
                    // Queue full, yield to consumer
                    std::this_thread::yield();
                }
            }
        }
    });
    
    // Transfer thread (simulates block-to-block transfer using dbf)
    std::thread transfer([&]() {
        while (transferred < total_samples) {
            // Try to use doubly-mapped buffers for transfer
            auto [read_ptr, read_size] = source_queue.read_dbf();
            auto [write_ptr, write_size] = dest_queue.write_dbf();
            
            if (read_ptr && write_ptr && read_size > 0 && write_size > 0) {
                // OPTIMAL PATH: Direct copy between doubly-mapped buffers
                size_t to_transfer = std::min(read_size, write_size);
                std::memcpy(write_ptr, read_ptr, to_transfer * sizeof(float));
                
                source_queue.commit_read(to_transfer);
                dest_queue.commit_write(to_transfer);
                transferred += to_transfer;
            } else if (read_ptr && read_size > 0) {
                // Semi-optimal: Read from dbf, write normally
                size_t space = dest_queue.space();
                size_t to_transfer = std::min(read_size, space);
                
                if (to_transfer > 0) {
                    for (size_t i = 0; i < to_transfer; i++) {
                        [[maybe_unused]] bool pushed = dest_queue.try_push(read_ptr[i]);
                    }
                    source_queue.commit_read(to_transfer);
                    transferred += to_transfer;
                }
            } else {
                // Fallback to peek/commit
                const float* p1, *p2;
                size_t s1, s2;
                size_t available = source_queue.peek_read(p1, s1, p2, s2);
                
                if (available > 0) {
                    size_t space = dest_queue.space();
                    size_t to_transfer = std::min(available, space);
                    
                    // Transfer from first segment
                    size_t from_s1 = std::min(s1, to_transfer);
                    for (size_t i = 0; i < from_s1; i++) {
                        [[maybe_unused]] bool pushed = dest_queue.try_push(p1[i]);
                    }
                    
                    // Transfer from second segment if needed
                    if (to_transfer > from_s1) {
                        size_t from_s2 = to_transfer - from_s1;
                        for (size_t i = 0; i < from_s2; i++) {
                            [[maybe_unused]] bool pushed = dest_queue.try_push(p2[i]);
                        }
                    }
                    
                    source_queue.commit_read(to_transfer);
                    transferred += to_transfer;
                }
            }
            
            if (source_queue.empty() && pushed == total_samples) {
                break;  // All data transferred
            }
            
            std::this_thread::yield();
        }
    });
    
    // Consumer thread - verify all data arrives in order
    std::thread consumer([&]() {
        size_t consumed = 0;
        float expected = 0.0f;
        
        while (consumed < total_samples) {
            float value;
            if (dest_queue.try_pop(value)) {
                EXPECT_EQ(value, expected) << "Sample mismatch at index " << consumed;
                expected += 1.0f;
                consumed++;
            } else {
                if (transferred == total_samples && dest_queue.empty()) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        
        EXPECT_EQ(consumed, total_samples) << "Not all samples were consumed";
    });
    
    // Wait for all threads
    producer.join();
    transfer.join();
    consumer.join();
    
    // Final verification
    EXPECT_EQ(pushed, total_samples) << "Not all samples were pushed";
    EXPECT_EQ(transferred, total_samples) << "Not all samples were transferred";
    EXPECT_TRUE(source_queue.empty()) << "Source queue not empty";
    EXPECT_TRUE(dest_queue.empty()) << "Destination queue not empty";
}

// Test write_dbf behavior to ensure proper handling
TEST_F(SPSCQueueDoublyMappedTest, WriteDbfCorrectness) {
    dro::SPSCQueue<float> queue(16384);  // 64KB
    
    // Test 1: Empty queue should provide maximum write space
    auto [write_ptr1, write_size1] = queue.write_dbf();
    if (write_ptr1) {
        EXPECT_GT(write_size1, 0);
        EXPECT_LE(write_size1, queue.capacity());
        
        // Write some data
        size_t to_write = std::min(size_t(1000), write_size1);
        for (size_t i = 0; i < to_write; i++) {
            write_ptr1[i] = static_cast<float>(i);
        }
        queue.commit_write(to_write);
        
        EXPECT_EQ(queue.size(), to_write);
    }
    
    // Test 2: Verify data integrity after write_dbf
    float value;
    for (size_t i = 0; i < 1000 && !queue.empty(); i++) {
        ASSERT_TRUE(queue.try_pop(value));
        EXPECT_EQ(value, static_cast<float>(i));
    }
    
    // Test 3: Fill queue to create wrap-around, then test write_dbf
    for (size_t i = 0; i < 12000; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // Consume some to create space at beginning
    for (size_t i = 0; i < 5000; i++) {
        [[maybe_unused]] bool pop_result = queue.try_pop(value);
    }
    
    // Now write_dbf should give us space at the beginning
    auto [write_ptr2, write_size2] = queue.write_dbf();
    if (write_ptr2) {
        EXPECT_GT(write_size2, 0);
        
        // Write sequential data
        size_t to_write = std::min(size_t(3000), write_size2);
        for (size_t i = 0; i < to_write; i++) {
            write_ptr2[i] = static_cast<float>(12000 + i);
        }
        queue.commit_write(to_write);
    }
    
    // Verify all data is in correct order
    float expected = 5000.0f;  // We consumed 0-4999
    while (!queue.empty()) {
        ASSERT_TRUE(queue.try_pop(value));
        EXPECT_EQ(value, expected);
        expected += 1.0f;
    }
}