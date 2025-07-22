#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <random>
#include <chrono>
#include <cstring>
#include "../../include/cler_spsc-queue.hpp"

class SPSCQueueBatchTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test basic writeN/readN operations
TEST_F(SPSCQueueBatchTest, BasicWriteNReadN) {
    dro::SPSCQueue<int> queue(100);
    
    // Test data
    std::vector<int> write_data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> read_data(write_data.size());
    
    // Write batch
    std::size_t written = queue.writeN(write_data.data(), write_data.size());
    EXPECT_EQ(written, write_data.size());
    EXPECT_EQ(queue.size(), write_data.size());
    
    // Read batch
    std::size_t read = queue.readN(read_data.data(), read_data.size());
    EXPECT_EQ(read, write_data.size());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());
    
    // Verify data integrity
    EXPECT_EQ(read_data, write_data);
}

// Test writeN/readN with wraparound
TEST_F(SPSCQueueBatchTest, WriteNReadNWrapAround) {
    dro::SPSCQueue<int> queue(8);
    
    // Fill half the queue first to create offset
    std::vector<int> initial_data = {100, 101, 102};
    queue.writeN(initial_data.data(), initial_data.size());
    
    std::vector<int> temp(initial_data.size());
    queue.readN(temp.data(), temp.size());
    
    // Now write data that will wrap around
    std::vector<int> wrap_data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::size_t written = queue.writeN(wrap_data.data(), wrap_data.size());
    
    // Read back and verify
    std::vector<int> read_data(written);
    std::size_t read = queue.readN(read_data.data(), written);
    EXPECT_EQ(read, written);
    
    for (std::size_t i = 0; i < read; ++i) {
        EXPECT_EQ(read_data[i], wrap_data[i]) << "Data mismatch at index " << i;
    }
}

// Test partial writeN when queue has limited space
TEST_F(SPSCQueueBatchTest, PartialWriteN) {
    dro::SPSCQueue<int> queue(5);
    
    // Pre-fill some data
    std::vector<int> prefill = {1, 2};
    queue.writeN(prefill.data(), prefill.size());
    EXPECT_EQ(queue.size(), 2);
    EXPECT_EQ(queue.space(), 3);
    
    // Try to write more than available space
    std::vector<int> large_data = {10, 11, 12, 13, 14, 15};
    std::size_t written = queue.writeN(large_data.data(), large_data.size());
    
    // Should only write what fits
    EXPECT_EQ(written, 3);  // Only 3 slots available
    EXPECT_EQ(queue.size(), 5);  // Queue is now full
    EXPECT_EQ(queue.space(), 0);
    
    // Read all and verify
    std::vector<int> read_data(5);
    std::size_t read = queue.readN(read_data.data(), 5);
    EXPECT_EQ(read, 5);
    
    // First 2 should be prefill, next 3 should be from large_data
    EXPECT_EQ(read_data[0], 1);
    EXPECT_EQ(read_data[1], 2);
    EXPECT_EQ(read_data[2], 10);
    EXPECT_EQ(read_data[3], 11);
    EXPECT_EQ(read_data[4], 12);
}

// Test readN when less data available than requested
TEST_F(SPSCQueueBatchTest, PartialReadN) {
    dro::SPSCQueue<int> queue(10);
    
    // Write small amount of data
    std::vector<int> small_data = {1, 2, 3};
    queue.writeN(small_data.data(), small_data.size());
    
    // Try to read more than available
    std::vector<int> large_buffer(10);
    std::size_t read = queue.readN(large_buffer.data(), 10);
    
    // Should only read what's available
    EXPECT_EQ(read, 3);
    EXPECT_EQ(large_buffer[0], 1);
    EXPECT_EQ(large_buffer[1], 2);
    EXPECT_EQ(large_buffer[2], 3);
    
    EXPECT_TRUE(queue.empty());
}

// Test peek_write/commit_write operations
TEST_F(SPSCQueueBatchTest, PeekWriteCommit) {
    dro::SPSCQueue<int> queue(10);
    
    int* ptr1;
    int* ptr2;
    std::size_t size1, size2;
    
    // Peek for write space
    std::size_t available = queue.peek_write(ptr1, size1, ptr2, size2);
    EXPECT_EQ(available, 10);  // Full capacity available
    EXPECT_NE(ptr1, nullptr);
    EXPECT_EQ(size1, 10);
    EXPECT_EQ(ptr2, nullptr);  // No wrap needed
    EXPECT_EQ(size2, 0);
    
    // Write to the peeked memory
    for (std::size_t i = 0; i < size1; ++i) {
        ptr1[i] = static_cast<int>(i + 1);
    }
    
    // Commit the write
    queue.commit_write(size1);
    EXPECT_EQ(queue.size(), 10);
    
    // Verify data can be read correctly
    std::vector<int> read_data(10);
    std::size_t read = queue.readN(read_data.data(), 10);
    EXPECT_EQ(read, 10);
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(read_data[i], i + 1);
    }
}

// Test peek_read/commit_read operations
TEST_F(SPSCQueueBatchTest, PeekReadCommit) {
    dro::SPSCQueue<int> queue(10);
    
    // First, write some data
    std::vector<int> test_data = {10, 20, 30, 40, 50};
    queue.writeN(test_data.data(), test_data.size());
    
    const int* ptr1;
    const int* ptr2;
    std::size_t size1, size2;
    
    // Peek for read
    std::size_t available = queue.peek_read(ptr1, size1, ptr2, size2);
    EXPECT_EQ(available, 5);
    EXPECT_NE(ptr1, nullptr);
    EXPECT_EQ(size1, 5);
    EXPECT_EQ(ptr2, nullptr);  // No wrap
    EXPECT_EQ(size2, 0);
    
    // Verify peeked data
    for (std::size_t i = 0; i < size1; ++i) {
        EXPECT_EQ(ptr1[i], test_data[i]);
    }
    
    // Commit partial read
    queue.commit_read(3);
    EXPECT_EQ(queue.size(), 2);
    
    // Verify remaining data
    std::vector<int> remaining(2);
    std::size_t read = queue.readN(remaining.data(), 2);
    EXPECT_EQ(read, 2);
    EXPECT_EQ(remaining[0], 40);
    EXPECT_EQ(remaining[1], 50);
}

// Test peek operations with wraparound
TEST_F(SPSCQueueBatchTest, PeekWithWrapAround) {
    dro::SPSCQueue<int> queue(6);
    
    // Fill and partially drain to create wraparound scenario
    std::vector<int> initial = {1, 2, 3, 4};
    queue.writeN(initial.data(), initial.size());
    
    std::vector<int> temp(2);
    queue.readN(temp.data(), 2);  // Read first 2, leaving 3,4 in queue
    
    // Now peek_write should show wraparound
    int* ptr1;
    int* ptr2;
    std::size_t size1, size2;
    
    std::size_t space = queue.peek_write(ptr1, size1, ptr2, size2);
    EXPECT_EQ(space, 4);  // 4 slots available
    
    if (size2 > 0) {  // Wraparound case
        EXPECT_NE(ptr1, nullptr);
        EXPECT_NE(ptr2, nullptr);
        EXPECT_EQ(size1 + size2, 4);
        
        // Write to both chunks
        for (std::size_t i = 0; i < size1; ++i) {
            ptr1[i] = 100 + static_cast<int>(i);
        }
        for (std::size_t i = 0; i < size2; ++i) {
            ptr2[i] = 200 + static_cast<int>(i);
        }
        
        queue.commit_write(4);
        EXPECT_EQ(queue.size(), 6);  // Queue should be full
    } else {  // Contiguous case
        for (std::size_t i = 0; i < size1; ++i) {
            ptr1[i] = 100 + static_cast<int>(i);
        }
        queue.commit_write(size1);
    }
    
    // Read all and verify order is maintained
    std::vector<int> all_data(queue.size());
    queue.readN(all_data.data(), queue.size());
    
    // Should start with remaining data (3, 4), then new data
    EXPECT_EQ(all_data[0], 3);
    EXPECT_EQ(all_data[1], 4);
    // Rest should be the written data in order
}

// Test no sample loss with batch operations
TEST_F(SPSCQueueBatchTest, NoSampleLossBatchOperations) {
    const int NUM_BATCHES = 100;
    const int BATCH_SIZE = 50;
    const int TOTAL_ITEMS = NUM_BATCHES * BATCH_SIZE;
    dro::SPSCQueue<int> queue(200);  // Larger than batch size
    
    std::vector<int> all_sent_data;
    std::vector<int> all_received_data;
    all_sent_data.reserve(TOTAL_ITEMS);
    all_received_data.reserve(TOTAL_ITEMS);
    
    // Producer thread using writeN
    std::thread producer([&]() {
        for (int batch = 0; batch < NUM_BATCHES; ++batch) {
            std::vector<int> batch_data(BATCH_SIZE);
            for (int i = 0; i < BATCH_SIZE; ++i) {
                batch_data[i] = batch * BATCH_SIZE + i;
            }
            
            // Write entire batch
            std::size_t written = 0;
            while (written < BATCH_SIZE) {
                std::size_t w = queue.writeN(batch_data.data() + written, BATCH_SIZE - written);
                written += w;
                if (w == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            
            // Store what we sent
            all_sent_data.insert(all_sent_data.end(), batch_data.begin(), batch_data.end());
        }
    });
    
    // Consumer thread using readN
    std::thread consumer([&]() {
        int total_read = 0;
        while (total_read < TOTAL_ITEMS) {
            std::vector<int> buffer(BATCH_SIZE);
            std::size_t read = queue.readN(buffer.data(), BATCH_SIZE);
            if (read > 0) {
                all_received_data.insert(all_received_data.end(), buffer.begin(), buffer.begin() + read);
                total_read += read;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    // Verify no sample loss and order preservation
    EXPECT_EQ(all_received_data.size(), TOTAL_ITEMS);
    EXPECT_EQ(all_sent_data.size(), TOTAL_ITEMS);
    
    for (int i = 0; i < TOTAL_ITEMS; ++i) {
        EXPECT_EQ(all_received_data[i], all_sent_data[i]) 
            << "Sample mismatch at index " << i;
    }
    
    EXPECT_TRUE(queue.empty());
}

// Test mixed operations (batch and single)
TEST_F(SPSCQueueBatchTest, MixedOperations) {
    dro::SPSCQueue<int> queue(20);
    
    // Mix writeN, push, readN, and pop operations
    std::vector<int> batch1 = {1, 2, 3};
    queue.writeN(batch1.data(), batch1.size());
    
    queue.push(4);
    queue.push(5);
    
    std::vector<int> batch2 = {6, 7, 8, 9};
    queue.writeN(batch2.data(), batch2.size());
    
    // Now read using mixed methods
    std::vector<int> read_batch(4);
    std::size_t read = queue.readN(read_batch.data(), 4);
    EXPECT_EQ(read, 4);
    
    int single_val;
    queue.pop(single_val);
    EXPECT_EQ(single_val, 5);
    
    // Read remaining
    std::vector<int> remaining(4);
    std::size_t remaining_read = queue.readN(remaining.data(), 4);
    EXPECT_EQ(remaining_read, 4);
    
    // Verify order
    EXPECT_EQ(read_batch[0], 1);
    EXPECT_EQ(read_batch[1], 2);
    EXPECT_EQ(read_batch[2], 3);
    EXPECT_EQ(read_batch[3], 4);
    
    EXPECT_EQ(remaining[0], 6);
    EXPECT_EQ(remaining[1], 7);
    EXPECT_EQ(remaining[2], 8);
    EXPECT_EQ(remaining[3], 9);
    
    EXPECT_TRUE(queue.empty());
}

// Test error conditions and edge cases
TEST_F(SPSCQueueBatchTest, EdgeCasesAndErrors) {
    dro::SPSCQueue<int> queue(5);
    
    // Test writeN/readN with empty queue
    std::vector<int> empty_read(10);
    EXPECT_EQ(queue.readN(empty_read.data(), 10), 0);
    
    // Test writeN/readN with full queue  
    std::vector<int> fill_data = {1, 2, 3, 4, 5};
    queue.writeN(fill_data.data(), fill_data.size());
    
    std::vector<int> overflow_data = {6, 7, 8};
    EXPECT_EQ(queue.writeN(overflow_data.data(), overflow_data.size()), 0);
    
    // Test peek with empty queue
    int* write_ptr1;
    int* write_ptr2;
    std::size_t write_size1, write_size2;
    
    const int* read_ptr1;
    const int* read_ptr2;
    std::size_t read_size1, read_size2;
    
    // Clear queue first
    std::vector<int> drain(5);
    queue.readN(drain.data(), 5);
    
    EXPECT_EQ(queue.peek_read(read_ptr1, read_size1, read_ptr2, read_size2), 0);
    EXPECT_EQ(read_ptr1, nullptr);
    EXPECT_EQ(read_ptr2, nullptr);
    EXPECT_EQ(read_size1, 0);
    EXPECT_EQ(read_size2, 0);
    
    // Test peek with full queue
    queue.writeN(fill_data.data(), fill_data.size());
    EXPECT_EQ(queue.peek_write(write_ptr1, write_size1, write_ptr2, write_size2), 0);
    EXPECT_EQ(write_ptr1, nullptr);
    EXPECT_EQ(write_ptr2, nullptr);
    EXPECT_EQ(write_size1, 0);
    EXPECT_EQ(write_size2, 0);
}