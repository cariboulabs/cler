#include <gtest/gtest.h>
#include "cler_embedded_allocators.hpp"
#include <thread>
#include <vector>
#include <atomic>

class AllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test StaticPoolAllocator
TEST_F(AllocatorTest, StaticPoolAllocator) {
    cler::StaticPoolAllocator<1024> allocator;
    
    // Test basic allocation
    auto* ptr1 = allocator.allocate<int>(10);
    ASSERT_NE(ptr1, nullptr);
    
    // Test memory usage tracking
    EXPECT_GT(allocator.bytes_used(), 0);
    EXPECT_LT(allocator.bytes_used(), 1024);
    EXPECT_EQ(allocator.bytes_used() + allocator.bytes_available(), 1024);
    
    // Test reset functionality
    allocator.reset();
    EXPECT_EQ(allocator.bytes_used(), 0);
    EXPECT_EQ(allocator.bytes_available(), 1024);
}

// Test StaticPoolAllocator exhaustion
TEST_F(AllocatorTest, StaticPoolAllocatorExhaustion) {
    cler::StaticPoolAllocator<64> small_allocator;  // Small pool
    
    // Allocate until exhaustion
    std::vector<int*> ptrs;
    bool allocation_failed = false;
    
    try {
        for (int i = 0; i < 100; ++i) {
            auto* ptr = small_allocator.allocate<int>(1);
            if (!ptr) break;
            ptrs.push_back(ptr);
        }
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    
    // Should have failed to allocate everything
    EXPECT_TRUE(allocation_failed || ptrs.size() < 100);
    EXPECT_GT(small_allocator.bytes_used(), 0);
}

// Test ThreadSafePoolAllocator
TEST_F(AllocatorTest, ThreadSafePoolAllocator) {
    cler::ThreadSafePoolAllocator<64, 16> allocator;
    
    // Test single-threaded allocation
    auto* ptr1 = allocator.allocate<int>(1);
    ASSERT_NE(ptr1, nullptr);
    
    auto* ptr2 = allocator.allocate<char>(1);
    ASSERT_NE(ptr2, nullptr);
    
    // Test deallocation
    allocator.deallocate(ptr1, 1);
    allocator.deallocate(ptr2, 1);
    
    // Should be able to reallocate
    auto* ptr3 = allocator.allocate<int>(1);
    ASSERT_NE(ptr3, nullptr);
    
    allocator.deallocate(ptr3, 1);
}

// Test ThreadSafePoolAllocator multi-threading
TEST_F(AllocatorTest, ThreadSafePoolAllocatorMultiThreaded) {
    cler::ThreadSafePoolAllocator<64, 128> allocator;
    constexpr int NUM_THREADS = 4;
    constexpr int ALLOCATIONS_PER_THREAD = 16;
    
    std::atomic<int> successful_allocations{0};
    std::atomic<int> successful_deallocations{0};
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            std::vector<int*> local_ptrs;
            
            // Allocate
            for (int i = 0; i < ALLOCATIONS_PER_THREAD; ++i) {
                try {
                    auto* ptr = allocator.allocate<int>(1);
                    if (ptr) {
                        local_ptrs.push_back(ptr);
                        successful_allocations++;
                    }
                } catch (...) {
                    // Allocation failed
                }
            }
            
            // Deallocate
            for (auto* ptr : local_ptrs) {
                allocator.deallocate(ptr, 1);
                successful_deallocations++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successful_allocations.load(), successful_deallocations.load());
    EXPECT_GT(successful_allocations.load(), 0);
}

// Test StackAllocator basic operations
TEST_F(AllocatorTest, StackAllocator) {
    cler::StackAllocator<1024> allocator;
    
    // Test basic allocation
    auto* ptr1 = allocator.allocate<int>(1);
    ASSERT_NE(ptr1, nullptr);
    
    size_t used_after_first = allocator.used();
    EXPECT_GT(used_after_first, 0);
    
    auto* ptr2 = allocator.allocate<double>(1);
    ASSERT_NE(ptr2, nullptr);
    
    size_t used_after_second = allocator.used();
    EXPECT_GT(used_after_second, used_after_first);
    
    // Test LIFO deallocation
    allocator.deallocate(ptr2, 1);
    EXPECT_EQ(allocator.used(), used_after_first);
    
    allocator.deallocate(ptr1, 1);
    EXPECT_EQ(allocator.used(), 0);
}

// Test StackAllocator marker functionality
TEST_F(AllocatorTest, StackAllocatorMarkers) {
    cler::StackAllocator<1024> allocator;
    
    // Get initial marker
    auto initial_marker = allocator.get_marker();
    EXPECT_EQ(initial_marker.offset, 0);
    
    // Allocate some memory
    auto* ptr1 = allocator.allocate<int>(10);
    auto* ptr2 = allocator.allocate<double>(5);
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    
    size_t used_before_marker = allocator.used();
    EXPECT_GT(used_before_marker, 0);
    
    // Get marker after allocations
    auto mid_marker = allocator.get_marker();
    EXPECT_EQ(mid_marker.offset, used_before_marker);
    
    // Allocate more memory
    auto* ptr3 = allocator.allocate<char>(20);
    ASSERT_NE(ptr3, nullptr);
    EXPECT_GT(allocator.used(), used_before_marker);
    
    // Free to mid marker
    allocator.free_to_marker(mid_marker);
    EXPECT_EQ(allocator.used(), used_before_marker);
    
    // Free to initial marker
    allocator.free_to_marker(initial_marker);
    EXPECT_EQ(allocator.used(), 0);
}

// Test RegionAllocator
TEST_F(AllocatorTest, RegionAllocator) {
    const size_t region_size = 256;
    static int memory_region[region_size];
    
    cler::RegionAllocator<int> allocator(memory_region, region_size);
    
    // Test basic allocation
    auto* ptr1 = allocator.allocate(10);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_GE(ptr1, memory_region);
    EXPECT_LT(ptr1, memory_region + region_size);
    
    auto* ptr2 = allocator.allocate(20);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1, ptr2);
    
    // Test reset
    allocator.reset();
    
    // Should be able to allocate again from the beginning
    auto* ptr3 = allocator.allocate(10);
    EXPECT_EQ(ptr3, memory_region);  // Should start from beginning again
}

// Test RegionAllocator exhaustion
TEST_F(AllocatorTest, RegionAllocatorExhaustion) {
    const size_t region_size = 10;  // Very small region
    static int memory_region[region_size];
    
    cler::RegionAllocator<int> allocator(memory_region, region_size);
    
    // Allocate all available memory
    auto* ptr1 = allocator.allocate(region_size);
    ASSERT_NE(ptr1, nullptr);
    
    // Next allocation should fail
    bool allocation_failed = false;
    try {
        auto* ptr2 = allocator.allocate(1);
        (void)ptr2;  // Suppress unused variable warning
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    
    EXPECT_TRUE(allocation_failed);
}

// Test allocator traits
TEST_F(AllocatorTest, AllocatorTraits) {
    // Test static allocator detection
    EXPECT_TRUE((cler::is_static_allocator<cler::StaticPoolAllocator<1024>>::value));
    EXPECT_TRUE((cler::is_static_allocator<cler::ThreadSafePoolAllocator<64, 16>>::value));
    EXPECT_TRUE((cler::is_static_allocator<cler::StackAllocator<1024>>::value));
    EXPECT_TRUE((cler::is_static_allocator<cler::RegionAllocator<int>>::value));
    
    // Test thread-safe allocator detection
    EXPECT_TRUE((cler::is_thread_safe_allocator<cler::ThreadSafePoolAllocator<64, 16>>::value));
    EXPECT_FALSE((cler::is_thread_safe_allocator<cler::StaticPoolAllocator<1024>>::value));
    EXPECT_FALSE((cler::is_thread_safe_allocator<cler::StackAllocator<1024>>::value));
    EXPECT_FALSE((cler::is_thread_safe_allocator<cler::RegionAllocator<int>>::value));
    
    // Test marker support detection
    EXPECT_TRUE((cler::supports_markers<cler::StackAllocator<1024>>::value));
    EXPECT_FALSE((cler::supports_markers<cler::StaticPoolAllocator<1024>>::value));
    EXPECT_FALSE((cler::supports_markers<cler::ThreadSafePoolAllocator<64, 16>>::value));
    EXPECT_FALSE((cler::supports_markers<cler::RegionAllocator<int>>::value));
}

// Test allocator rebind functionality
TEST_F(AllocatorTest, AllocatorRebind) {
    // Test ThreadSafePoolAllocator rebind
    using IntAllocator = cler::ThreadSafePoolAllocator<64, 16>;
    using CharAllocator = IntAllocator::rebind<char>::other;
    
    static_assert(std::is_same_v<CharAllocator, cler::ThreadSafePoolAllocator<64, 16>>);
    
    // Test StaticPoolAllocator rebind
    using IntPoolAllocator = cler::StaticPoolAllocator<1024>;
    using DoublePoolAllocator = IntPoolAllocator::rebind<double>::other;
    
    static_assert(std::is_same_v<DoublePoolAllocator, cler::StaticPoolAllocator<1024>>);
}

// Performance test for allocators
TEST_F(AllocatorTest, AllocatorPerformance) {
    constexpr int NUM_ALLOCATIONS = 1000;
    
    // Test ThreadSafePoolAllocator performance
    {
        cler::ThreadSafePoolAllocator<64, NUM_ALLOCATIONS> allocator;
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<int*> ptrs;
        for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
            auto* ptr = allocator.allocate<int>(1);
            if (ptr) ptrs.push_back(ptr);
        }
        
        for (auto* ptr : ptrs) {
            allocator.deallocate(ptr, 1);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Just verify it completes in reasonable time (< 10ms)
        EXPECT_LT(duration.count(), 10000);
    }
}