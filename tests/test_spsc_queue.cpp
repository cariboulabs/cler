#include <gtest/gtest.h>
#include "cler_spsc-queue.hpp"
#include "cler_embedded_allocators.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>

class SPSCQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test basic stack allocation (embedded-friendly)
TEST_F(SPSCQueueTest, StackAllocation) {
    dro::SPSCQueue<int, 1024> stack_queue;
    
    EXPECT_EQ(stack_queue.capacity(), 1024);
    EXPECT_TRUE(stack_queue.empty());
    EXPECT_EQ(stack_queue.size(), 0);
}

// Test basic dynamic allocation (no std::vector)
TEST_F(SPSCQueueTest, DynamicAllocation) {
    dro::SPSCQueue<int> dynamic_queue(2048);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(dynamic_queue.is_valid());
#endif
    
    EXPECT_EQ(dynamic_queue.capacity(), 2048);
    EXPECT_TRUE(dynamic_queue.empty());
    EXPECT_EQ(dynamic_queue.size(), 0);
}

// Test basic push/pop operations
TEST_F(SPSCQueueTest, BasicPushPop) {
    dro::SPSCQueue<int, 512> queue;
    
    // Test single push/pop
    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    
    int value;
    queue.pop(value);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

// Test try_push and try_pop operations
TEST_F(SPSCQueueTest, TryOperations) {
    dro::SPSCQueue<int, 4> small_queue;  // Small queue for testing overflow
    
    // Fill the queue
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(small_queue.try_push(i));
    }
    
    // Queue should be full now
    EXPECT_FALSE(small_queue.try_push(999));
    
    // Test try_pop
    int value;
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(small_queue.try_pop(value));
        EXPECT_EQ(value, i);
    }
    
    // Queue should be empty now
    EXPECT_FALSE(small_queue.try_pop(value));
}

// Test emplace operations
TEST_F(SPSCQueueTest, EmplaceOperations) {
    dro::SPSCQueue<std::pair<int, int>, 16> queue;
    
    // Test emplace
    queue.emplace(1, 2);
    EXPECT_EQ(queue.size(), 1);
    
    // Test try_emplace
    EXPECT_TRUE(queue.try_emplace(3, 4));
    EXPECT_EQ(queue.size(), 2);
    
    // Verify values
    std::pair<int, int> value;
    queue.pop(value);
    EXPECT_EQ(value.first, 1);
    EXPECT_EQ(value.second, 2);
    
    queue.pop(value);
    EXPECT_EQ(value.first, 3);
    EXPECT_EQ(value.second, 4);
}

// Test custom allocators - Static Pool
TEST_F(SPSCQueueTest, StaticPoolAllocator) {
    cler::StaticPoolAllocator<8192> pool_alloc;
    dro::SPSCQueue<int, 0, cler::StaticPoolAllocator<8192>> queue(256, pool_alloc);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(queue.is_valid());
#endif
    
    // Test basic operations with custom allocator
    queue.push(123);
    queue.push(456);
    
    int val1, val2;
    queue.pop(val1);
    queue.pop(val2);
    
    EXPECT_EQ(val1, 123);
    EXPECT_EQ(val2, 456);
}

// Test custom allocators - Region Allocator
TEST_F(SPSCQueueTest, RegionAllocator) {
    const size_t region_size = 1024;
    static int memory_region[region_size];
    
    cler::RegionAllocator<int> region_alloc(memory_region, region_size);
    dro::SPSCQueue<int, 0, cler::RegionAllocator<int>> queue(64, region_alloc);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(queue.is_valid());
#endif
    
    // Test with specific memory region
    queue.push(789);
    
    int value;
    queue.pop(value);
    EXPECT_EQ(value, 789);
}

// Test multi-threaded producer-consumer
TEST_F(SPSCQueueTest, MultiThreadedPerformance) {
    dro::SPSCQueue<int> queue(8192);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(queue.is_valid());
#endif
    
    constexpr int NUM_ITEMS = 10000;
    std::atomic<bool> writer_done{false};
    std::atomic<int> items_read{0};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }
        writer_done = true;
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int value;
        int count = 0;
        while (count < NUM_ITEMS) {
            if (queue.try_pop(value)) {
                EXPECT_EQ(value, count);  // Verify order is maintained
                count++;
            } else {
                std::this_thread::yield();
            }
        }
        items_read = count;
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(items_read.load(), NUM_ITEMS);
    EXPECT_TRUE(queue.empty());
}

// Test cache line detection (compilation test)
TEST_F(SPSCQueueTest, CacheLineDetection) {
    // This test mainly verifies that the cache line detection compiles
    // and doesn't crash on different platforms
    dro::SPSCQueue<char, 128> queue;
    
    // Test that alignment works correctly
    queue.push('A');
    char value;
    queue.pop(value);
    EXPECT_EQ(value, 'A');
}

// Test exception-free mode (if enabled)
#ifdef DRO_SPSC_NO_EXCEPTIONS
TEST_F(SPSCQueueTest, ExceptionFreeMode) {
    // Test invalid capacity
    dro::SPSCQueue<int> invalid_queue(0);  // Invalid capacity
    EXPECT_FALSE(invalid_queue.is_valid());
    EXPECT_NE(invalid_queue.get_construction_error(), dro::SPSCError::SUCCESS);
    
    // Test valid queue
    dro::SPSCQueue<int> valid_queue(64);
    EXPECT_TRUE(valid_queue.is_valid());
    EXPECT_EQ(valid_queue.get_construction_error(), dro::SPSCError::SUCCESS);
}
#endif

// Test large data types
TEST_F(SPSCQueueTest, LargeDataTypes) {
    struct LargeStruct {
        std::array<int, 64> data;
        LargeStruct() { data.fill(42); }
    };
    
    dro::SPSCQueue<LargeStruct, 16> queue;
    
    LargeStruct large_obj;
    queue.push(large_obj);
    
    LargeStruct retrieved;
    queue.pop(retrieved);
    
    EXPECT_EQ(retrieved.data[0], 42);
    EXPECT_EQ(retrieved.data[63], 42);
}

// Test bulk operations (writeN/readN)
TEST_F(SPSCQueueTest, BulkOperations) {
    dro::SPSCQueue<int> queue(1024);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(queue.is_valid());
#endif
    
    // Test writeN
    std::vector<int> write_data(100);
    std::iota(write_data.begin(), write_data.end(), 0);
    
    size_t written = queue.writeN(write_data.data(), write_data.size());
    EXPECT_EQ(written, 100);
    EXPECT_EQ(queue.size(), 100);
    
    // Test readN
    std::vector<int> read_data(100);
    size_t read_count = queue.readN(read_data.data(), read_data.size());
    EXPECT_EQ(read_count, 100);
    EXPECT_EQ(queue.size(), 0);
    
    // Verify data integrity
    for (size_t i = 0; i < 100; ++i) {
        EXPECT_EQ(read_data[i], static_cast<int>(i));
    }
}