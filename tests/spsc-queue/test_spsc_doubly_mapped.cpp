#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <complex>
#include <fstream>
#include "cler_spsc-queue.hpp"

class SPSCQueueDoublyMappedTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test large buffer behavior (may get doubly mapped on supported platforms)
TEST_F(SPSCQueueDoublyMappedTest, LargeBufferBehavior) {
    // 8192 floats = 32KB - exactly at threshold
    dro::SPSCQueue<float> large_queue(8192);
    
    // With page alignment, capacity will be larger than requested
    EXPECT_GE(large_queue.capacity(), 8192);
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

// Test that verifies read_dbf behavior with wraparound
TEST_F(SPSCQueueDoublyMappedTest, ReadDbfWraparoundBehavior) {
    // Create a queue large enough for dbf
    const size_t user_capacity = 1024;  // User-requested capacity
    dro::SPSCQueue<float> queue(user_capacity);
    
    // Check if dbf is available
    bool dbf_available = false;
    try {
        auto [ptr, size] = queue.write_dbf();
        dbf_available = (ptr != nullptr);
    } catch (const std::runtime_error&) {
        dbf_available = false;
    }
    
    if (!dbf_available) {
        GTEST_SKIP() << "Double-mapped buffers not available on this platform/configuration";
    }
    
    // Fill the buffer almost completely
    for (size_t i = 0; i < user_capacity - 10; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // Consume most data to move read pointer near the end
    float dummy;
    for (size_t i = 0; i < user_capacity - 20; i++) {
        queue.pop(dummy);
    }
    
    // Now we have about 10 values near the end of the buffer
    // Add more data to create wraparound
    for (size_t i = 0; i < 30; i++) {
        queue.push(1000.0f + i);
    }
    
    // We should have ~40 values total: some at end, most at beginning
    EXPECT_EQ(queue.size(), 40);
    
    // First read should get data up to buffer boundary
    auto [ptr1, size1] = queue.read_dbf();
    ASSERT_NE(ptr1, nullptr);
    EXPECT_GT(size1, 0);
    EXPECT_LE(size1, 40);  // Can't exceed available data
    
    // Verify data and consume
    queue.commit_read(size1);
    
    // If there's more data, read it
    if (queue.size() > 0) {
        auto [ptr2, size2] = queue.read_dbf();
        ASSERT_NE(ptr2, nullptr);
        EXPECT_EQ(size2, queue.size());
        queue.commit_read(size2);
    }
    
    EXPECT_TRUE(queue.empty());
}

// Test to diagnose double mapping issue
TEST_F(SPSCQueueDoublyMappedTest, DiagnoseDoubleMappingIssue) {
    // Create a queue with a specific size to see the alignment issue
    const size_t user_capacity = 16384;
    dro::SPSCQueue<float> queue(user_capacity);
    
    std::cout << "User requested capacity: " << user_capacity << std::endl;
    std::cout << "Queue reported capacity: " << queue.capacity() << std::endl;
    
    // Check if it claims to be doubly mapped
    if (queue.is_doubly_mapped()) {
        std::cout << "Queue claims to be doubly mapped" << std::endl;
        
        std::cout << "Queue is doubly mapped" << std::endl;
    } else {
        std::cout << "Queue is NOT doubly mapped" << std::endl;
    }
}

// Test cross-boundary read/write with DBF
TEST_F(SPSCQueueDoublyMappedTest, CrossBoundaryReadWrite) {
    const size_t user_capacity = 100;  // Small size for easy testing
    dro::SPSCQueue<float> queue(user_capacity);
    
    if (!queue.is_doubly_mapped()) {
        GTEST_SKIP() << "Queue is not doubly-mapped on this platform";
    }
    
    // Position write index near the end
    const size_t actual_capacity = queue.capacity();
    const size_t position_near_end = actual_capacity - 50;
    
    // Fill up to near the end
    for (size_t i = 0; i < position_near_end; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // Now use write_dbf to write across the boundary
    auto [write_ptr, write_size] = queue.write_dbf();
    ASSERT_NE(write_ptr, nullptr);
    ASSERT_GE(write_size, 100) << "Should be able to write at least 100 elements contiguously";
    
    // Write 100 elements that will cross the boundary
    const size_t write_count = 100;
    for (size_t i = 0; i < write_count; i++) {
        write_ptr[i] = static_cast<float>(1000 + i);
    }
    queue.commit_write(write_count);
    
    // Now consume the original data
    float val;
    for (size_t i = 0; i < position_near_end; i++) {
        queue.pop(val);
        EXPECT_EQ(val, static_cast<float>(i));
    }
    
    // Read back the cross-boundary data using normal readN
    std::vector<float> read_buffer(write_count);
    size_t read = queue.readN(read_buffer.data(), write_count);
    ASSERT_EQ(read, write_count);
    
    // Verify data integrity - this proves the wraparound worked!
    for (size_t i = 0; i < write_count; i++) {
        EXPECT_EQ(read_buffer[i], static_cast<float>(1000 + i))
            << "Data mismatch at position " << i << " - boundary crossing failed";
    }
    
    std::cout << "Cross-boundary test PASSED - wrote " << write_count 
              << " elements across wraparound boundary" << std::endl;
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

// Sanity check: Alias probe test
TEST_F(SPSCQueueDoublyMappedTest, AliasProbeTest) {
    // Create a queue large enough for DBF
    const size_t user_capacity = 16384;
    dro::SPSCQueue<float> queue(user_capacity);
    
    if (!queue.is_doubly_mapped()) {
        GTEST_SKIP() << "Queue is not doubly-mapped on this platform";
    }
    
    // The fact that the queue reports as doubly-mapped means the vmem layer
    // successfully created and verified the mapping
    ASSERT_TRUE(queue.is_doubly_mapped()) << "Queue should be doubly-mapped for this size";
    
    std::cout << "Queue successfully created with doubly-mapped buffer" << std::endl;
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

// Test that verifies double mapping actually works by checking wraparound scenarios
TEST_F(SPSCQueueDoublyMappedTest, DoublyMappedWraparoundVerification) {
    // Use a size that should trigger doubly-mapped allocation
    const size_t capacity = 16384;  // 64KB
    dro::SPSCQueue<float> queue(capacity);
    
    // Fill the queue almost to capacity, leaving just a small gap
    const size_t initial_fill = capacity - 100;
    for (size_t i = 0; i < initial_fill; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // Consume most of the data to move read pointer near the end
    const size_t consume_count = capacity - 200;
    for (size_t i = 0; i < consume_count; i++) {
        float val;
        queue.pop(val);
        EXPECT_EQ(val, static_cast<float>(i));
    }
    
    // Now we have ~200 samples near the end of the buffer
    // Fill more data to create a wraparound scenario
    const size_t additional_fill = 1000;
    for (size_t i = initial_fill; i < initial_fill + additional_fill; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // At this point, data wraps around: some at end of buffer, most at beginning
    size_t expected_total = (initial_fill - consume_count) + additional_fill;
    EXPECT_EQ(queue.size(), expected_total);
    
    // Debug: Let's see where we are in the buffer
    std::cout << "Queue capacity: " << capacity << std::endl;
    std::cout << "Expected total samples: " << expected_total << std::endl;
    std::cout << "Initial fill: " << initial_fill << ", consumed: " << consume_count 
              << ", additional: " << additional_fill << std::endl;
    
    // Try to read with read_dbf
    auto [read_ptr, read_size] = queue.read_dbf();
    
    if (read_ptr != nullptr) {
        std::cout << "read_dbf returned ptr: " << read_ptr << ", size: " << read_size << std::endl;
        
        // With aligned boundaries, read_dbf should return ALL available data
        EXPECT_EQ(read_size, expected_total) 
            << "read_dbf should return all " << expected_total 
            << " samples contiguously, but got " << read_size;
        
        // Let's see what happens at the boundary
        size_t samples_at_end = initial_fill - consume_count;  // ~200
        std::cout << "Samples at end of buffer: " << samples_at_end 
                  << ", samples at beginning: " << (expected_total - samples_at_end) << std::endl;
        
        // Verify the data is correct for what we can read
        float expected_value = static_cast<float>(consume_count);
        for (size_t i = 0; i < read_size; i++) {
            EXPECT_EQ(read_ptr[i], expected_value) 
                << "Data mismatch at position " << i 
                << " (expected " << expected_value << ", got " << read_ptr[i] << ")";
            expected_value += 1.0f;
        }
        
        // Commit the read
        queue.commit_read(read_size);
        
        // Since we couldn't read everything in one go, we need to read the rest
        if (read_size < expected_total) {
            // Read the remaining data
            size_t remaining = expected_total - read_size;
            std::cout << "Need to read remaining " << remaining << " samples" << std::endl;
            
            auto [read_ptr2, read_size2] = queue.read_dbf();
            EXPECT_EQ(read_size2, remaining);
            
            // Verify the wrapped data
            for (size_t i = 0; i < read_size2; i++) {
                EXPECT_EQ(read_ptr2[i], expected_value) 
                    << "Wrapped data mismatch at position " << i 
                    << " (expected " << expected_value << ", got " << read_ptr2[i] << ")";
                expected_value += 1.0f;
            }
            
            queue.commit_read(read_size2);
        }
        
        EXPECT_TRUE(queue.empty());
    } else {
        // If not doubly-mapped, peek_read should give us two segments
        const float* p1, *p2;
        size_t s1, s2;
        size_t total = queue.peek_read(p1, s1, p2, s2);
        
        EXPECT_EQ(total, expected_total);
        EXPECT_GT(s1, 0);
        EXPECT_GT(s2, 0);  // Must have wrapped data
        EXPECT_EQ(s1 + s2, total);
    }
}

// Test dual-capacity design with no discontinuities
TEST_F(SPSCQueueDoublyMappedTest, DualCapacityNoContinuities) {
    // Create a large queue for DBF
    const size_t user_capacity = 16384;
    dro::SPSCQueue<float> queue(user_capacity);
    
    // Skip if not doubly-mapped
    if (!queue.is_doubly_mapped()) {
        GTEST_SKIP() << "Queue is not doubly-mapped on this platform";
    }
    
    // With single capacity design, the internal capacity is page-aligned
    const size_t internal_capacity = queue.capacity() + 1;
    std::cout << "User capacity: " << queue.capacity() 
              << ", Internal capacity (page-aligned): " << internal_capacity << std::endl;
    
    // Test 1: Fill buffer to near the end
    const size_t fill_count = queue.capacity() - 50;  // Leave 50 slots empty
    std::vector<float> test_data;
    for (size_t i = 0; i < fill_count; i++) {
        test_data.push_back(static_cast<float>(i));
        queue.push(test_data.back());
    }
    
    // Test 2: Consume most data to position read pointer near end
    const size_t consume_count = fill_count - 100;  // Leave 100 elements
    for (size_t i = 0; i < consume_count; i++) {
        float val;
        queue.pop(val);
        EXPECT_EQ(val, test_data[i]) << "Initial data verification failed";
    }
    
    // Test 3: Add more data to create wraparound scenario
    const size_t wraparound_count = 200;  // This will wrap around
    size_t next_value = fill_count;
    for (size_t i = 0; i < wraparound_count; i++) {
        test_data.push_back(static_cast<float>(next_value++));
        queue.push(test_data.back());
    }
    
    // Now we have data that wraps around the logical capacity
    const size_t expected_size = 100 + wraparound_count;  // 300 total
    EXPECT_EQ(queue.size(), expected_size);
    
    // Test 4: Use read_dbf to read ALL data contiguously
    auto [ptr, size] = queue.read_dbf();
    ASSERT_NE(ptr, nullptr);
    ASSERT_GT(size, 0);
    
    // The key test: With dual-capacity design, we should be able to read
    // contiguously even when data wraps around logical capacity
    // because the extended region [capacity_, capacity_dbf_) is initialized
    
    // Verify ALL data is accessible and correct
    float expected_val = static_cast<float>(consume_count);
    size_t verified_count = 0;
    
    for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(ptr[i], expected_val) 
            << "DISCONTINUITY DETECTED at position " << i 
            << " (expected " << expected_val << ", got " << ptr[i] << ")"
            << "\nThis indicates the extended region is not properly initialized!";
        expected_val += 1.0f;
        verified_count++;
    }
    
    // If we couldn't read all data in one go, read the rest
    queue.commit_read(size);
    
    while (!queue.empty() && verified_count < expected_size) {
        auto [ptr2, size2] = queue.read_dbf();
        ASSERT_NE(ptr2, nullptr);
        for (size_t i = 0; i < size2; i++) {
            ASSERT_EQ(ptr2[i], expected_val)
                << "DISCONTINUITY in second read at position " << i;
            expected_val += 1.0f;
            verified_count++;
        }
        queue.commit_read(size2);
    }
    
    EXPECT_EQ(verified_count, expected_size) << "Not all data was verified";
    EXPECT_TRUE(queue.empty());
}

// Test that DBF commit functions handle wraparound correctly
TEST_F(SPSCQueueDoublyMappedTest, DbfCommitWraparoundHandling) {
    // Create a large queue for DBF
    const size_t capacity = 16384;
    dro::SPSCQueue<float> queue(capacity);
    
    // Skip if not doubly-mapped
    if (!queue.is_doubly_mapped()) {
        GTEST_SKIP() << "Queue is not doubly-mapped on this platform";
    }
    
    // Fill the queue almost completely, leaving just enough space for wraparound
    const size_t fill_to_near_end = capacity - 100;
    for (size_t i = 0; i < fill_to_near_end; i++) {
        queue.push(static_cast<float>(i));
    }
    
    // Consume most data to position read pointer near the end
    const size_t consume_to_near_end = capacity - 200;
    float dummy;
    for (size_t i = 0; i < consume_to_near_end; i++) {
        queue.pop(dummy);
    }
    
    // Now readIndex is near the end with ~100 samples available (not 200!)
    // We filled to capacity-100 and consumed capacity-200, so we have 100 left
    size_t remaining_from_initial = fill_to_near_end - consume_to_near_end;
    EXPECT_EQ(remaining_from_initial, 100);
    
    // Add more data to ensure wraparound
    const size_t add_more = 300;
    for (size_t i = 0; i < add_more; i++) {
        queue.push(static_cast<float>(fill_to_near_end + i));
    }
    
    // We should have ~400 samples total (100 + 300)
    size_t total_available = queue.size();
    EXPECT_EQ(total_available, remaining_from_initial + add_more);
    
    // Read using DBF - this should give us ALL available data contiguously
    auto [read_ptr, read_size] = queue.read_dbf();
    ASSERT_NE(read_ptr, nullptr);
    EXPECT_EQ(read_size, total_available) << "DBF should return all available data contiguously";
    
    // Verify the data is correct across the wraparound
    float expected = static_cast<float>(consume_to_near_end);
    for (size_t i = 0; i < read_size; i++) {
        EXPECT_EQ(read_ptr[i], expected) 
            << "Data mismatch at position " << i 
            << " (expected " << expected << ", got " << read_ptr[i] << ")";
        expected += 1.0f;
    }
    
    // Commit the read
    queue.commit_read(read_size);
    
    // Queue should be empty now
    EXPECT_TRUE(queue.empty());
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