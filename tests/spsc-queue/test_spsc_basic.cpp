#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <random>
#include <chrono>
#include "../../include/cler_spsc-queue.hpp"

class SPSCQueueBasicTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test basic push/pop operations with heap allocation
TEST_F(SPSCQueueBasicTest, BasicPushPop) {
    dro::SPSCQueue<int> queue(10);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.capacity(), 10);
    EXPECT_EQ(queue.space(), 10);
    
    // Test basic push/pop
    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    EXPECT_EQ(queue.space(), 9);
    
    int value;
    queue.pop(value);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.space(), 10);
}

// Test basic operations with stack allocation
TEST_F(SPSCQueueBasicTest, BasicPushPopStack) {
    dro::SPSCQueue<int, 10> queue(0);  // Stack allocation with size 10
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.capacity(), 10);
    
    queue.push(123);
    EXPECT_EQ(queue.size(), 1);
    
    int value;
    queue.pop(value);
    EXPECT_EQ(value, 123);
    EXPECT_TRUE(queue.empty());
}

// Test try_push and try_pop operations
TEST_F(SPSCQueueBasicTest, TryOperations) {
    dro::SPSCQueue<int> queue(3);
    
    // Fill the queue
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_TRUE(queue.try_push(3));
    
    // Queue should be full now
    EXPECT_FALSE(queue.try_push(4));
    EXPECT_EQ(queue.size(), 3);
    
    // Test try_pop
    int value;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 3);
    
    // Queue should be empty now
    EXPECT_FALSE(queue.try_pop(value));
    EXPECT_TRUE(queue.empty());
}

// Test wraparound behavior
TEST_F(SPSCQueueBasicTest, WrapAround) {
    dro::SPSCQueue<int> queue(4);
    
    // Fill and drain multiple times to test wraparound
    for (int cycle = 0; cycle < 3; ++cycle) {
        // Fill the queue
        for (int i = 0; i < 4; ++i) {
            queue.push(cycle * 4 + i);
        }
        EXPECT_EQ(queue.size(), 4);
        
        // Drain the queue
        for (int i = 0; i < 4; ++i) {
            int value;
            queue.pop(value);
            EXPECT_EQ(value, cycle * 4 + i);
        }
        EXPECT_TRUE(queue.empty());
    }
}

// Test no sample loss in sequential operations (using try_push to avoid blocking)
TEST_F(SPSCQueueBasicTest, NoSampleLossSequential) {
    const int NUM_ITEMS = 100;  // Reduced to match queue capacity
    dro::SPSCQueue<int> queue(NUM_ITEMS);
    
    // Producer: push all items (this should fit exactly in queue capacity)
    for (int i = 0; i < NUM_ITEMS; ++i) {
        queue.push(i);
    }
    
    // Consumer: pop all items and verify
    std::vector<int> received;
    int value;
    for (int i = 0; i < NUM_ITEMS; ++i) {
        queue.pop(value);
        received.push_back(value);
    }
    
    // Verify no samples were lost and order is preserved
    EXPECT_EQ(received.size(), NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i) << "Sample lost or reordered at index " << i;
    }
    
    EXPECT_TRUE(queue.empty());
}

// Test no sample loss with try_push (non-blocking)
TEST_F(SPSCQueueBasicTest, NoSampleLossNonBlocking) {
    const int QUEUE_SIZE = 50;
    const int NUM_CYCLES = 20;
    dro::SPSCQueue<int> queue(QUEUE_SIZE);
    
    std::vector<int> all_sent;
    std::vector<int> all_received;
    
    // Multiple cycles of fill and drain
    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        // Fill queue using try_push
        int items_pushed = 0;
        for (int i = 0; i < QUEUE_SIZE; ++i) {
            int value = cycle * QUEUE_SIZE + i;
            if (queue.try_push(value)) {
                all_sent.push_back(value);
                items_pushed++;
            } else {
                break; // Queue full
            }
        }
        
        // Drain queue using try_pop
        int value;
        while (queue.try_pop(value)) {
            all_received.push_back(value);
        }
    }
    
    // Verify all sent data was received in order
    EXPECT_EQ(all_received.size(), all_sent.size());
    for (size_t i = 0; i < all_received.size(); ++i) {
        EXPECT_EQ(all_received[i], all_sent[i]) << "Sample mismatch at index " << i;
    }
}

// Test concurrent producer/consumer with no sample loss
TEST_F(SPSCQueueBasicTest, NoSampleLossConcurrent) {
    const int NUM_ITEMS = 10000;
    const int QUEUE_SIZE = 64;
    dro::SPSCQueue<int> queue(QUEUE_SIZE);
    
    std::vector<int> received;
    received.reserve(NUM_ITEMS);
    
    // Consumer thread
    std::thread consumer([&queue, &received, NUM_ITEMS]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            int value;
            queue.pop(value);  // Blocking pop
            received.push_back(value);
        }
    });
    
    // Producer thread
    std::thread producer([&queue, NUM_ITEMS]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            queue.push(i);  // Blocking push
        }
    });
    
    producer.join();
    consumer.join();
    
    // Verify no samples were lost
    EXPECT_EQ(received.size(), NUM_ITEMS);
    
    // Sort and verify all values are present
    std::sort(received.begin(), received.end());
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i) << "Sample " << i << " was lost or duplicated";
    }
    
    EXPECT_TRUE(queue.empty());
}

// Test edge cases
TEST_F(SPSCQueueBasicTest, EdgeCases) {
    // Test minimum capacity
    dro::SPSCQueue<int> small_queue(1);
    EXPECT_EQ(small_queue.capacity(), 1);
    
    small_queue.push(42);
    EXPECT_TRUE(small_queue.space() == 0);  // Full
    
    int value;
    small_queue.pop(value);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(small_queue.empty());
}