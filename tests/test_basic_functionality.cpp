#include <gtest/gtest.h>
#include "cler_spsc-queue.hpp"
#include "cler_embedded_allocators.hpp"

class SimpleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test basic SPSC queue functionality
TEST_F(SimpleTest, BasicSPSCQueue) {
    dro::SPSCQueue<int, 128> queue;
    
    // Test basic operations
    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    
    int value;
    queue.pop(value);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

// Test basic allocator functionality  
TEST_F(SimpleTest, BasicAllocator) {
    cler::StaticPoolAllocator<1024> allocator;
    
    auto* ptr = allocator.allocate<int>(1);
    ASSERT_NE(ptr, nullptr);
    
    *ptr = 123;
    EXPECT_EQ(*ptr, 123);
    
    allocator.deallocate(ptr, 1);
}