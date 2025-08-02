#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>
#include <memory>
#include <cstring>
#include "cler_spsc-queue.hpp"

class SPSCQueueBoundsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test bounds with exact capacity peek/commit operations
TEST_F(SPSCQueueBoundsTest, PeekCommitExactCapacity) {
    const std::size_t CAPACITY = 512;
    dro::SPSCQueue<int> queue(CAPACITY);
    
    int* write_ptr1;
    int* write_ptr2;
    std::size_t write_size1, write_size2;
    
    // Peek for writing - should give us access to exactly CAPACITY elements
    std::size_t available_space = queue.peek_write(write_ptr1, write_size1, write_ptr2, write_size2);
    
    EXPECT_EQ(available_space, CAPACITY);
    EXPECT_NE(write_ptr1, nullptr);
    
    // Test that we can safely write to all available positions
    std::size_t total_written = 0;
    
    // Write to first chunk
    if (write_ptr1 && write_size1 > 0) {
        for (std::size_t i = 0; i < write_size1; ++i) {
            write_ptr1[i] = static_cast<int>(i);
        }
        total_written += write_size1;
    }
    
    // Write to second chunk if it exists (wraparound case)
    if (write_ptr2 && write_size2 > 0) {
        for (std::size_t i = 0; i < write_size2; ++i) {
            write_ptr2[i] = static_cast<int>(write_size1 + i);
        }
        total_written += write_size2;
    }
    
    EXPECT_EQ(total_written, CAPACITY);
    
    // Commit the write
    queue.commit_write(CAPACITY);
    EXPECT_EQ(queue.size(), CAPACITY);
    
    // Now test reading back all the data
    const int* read_ptr1;
    const int* read_ptr2;
    std::size_t read_size1, read_size2;
    
    std::size_t available_data = queue.peek_read(read_ptr1, read_size1, read_ptr2, read_size2);
    EXPECT_EQ(available_data, CAPACITY);
    
    // Verify we can read all data without bounds issues
    std::vector<int> read_data;
    read_data.reserve(CAPACITY);
    
    // Read first chunk
    if (read_ptr1 && read_size1 > 0) {
        for (std::size_t i = 0; i < read_size1; ++i) {
            read_data.push_back(read_ptr1[i]);
        }
    }
    
    // Read second chunk if exists
    if (read_ptr2 && read_size2 > 0) {
        for (std::size_t i = 0; i < read_size2; ++i) {
            read_data.push_back(read_ptr2[i]);
        }
    }
    
    EXPECT_EQ(read_data.size(), CAPACITY);
    
    // Verify data integrity
    for (std::size_t i = 0; i < CAPACITY; ++i) {
        EXPECT_EQ(read_data[i], static_cast<int>(i)) 
            << "Data corruption at index " << i;
    }
    
    // Commit the read
    queue.commit_read(CAPACITY);
    EXPECT_TRUE(queue.empty());
}

// Test bounds with writeN/readN operations at capacity
TEST_F(SPSCQueueBoundsTest, WriteNReadNExactCapacity) {
    const std::size_t CAPACITY = 1024;
    dro::SPSCQueue<int> queue(CAPACITY);
    
    // Create test data of exact capacity size
    std::vector<int> write_data(CAPACITY);
    std::iota(write_data.begin(), write_data.end(), 0);
    
    // Write exactly CAPACITY elements
    std::size_t written = queue.writeN(write_data.data(), CAPACITY);
    EXPECT_EQ(written, CAPACITY);
    EXPECT_EQ(queue.size(), CAPACITY);
    
    // With page alignment, actual capacity may be larger
    const std::size_t actual_capacity = queue.capacity();
    EXPECT_GE(actual_capacity, CAPACITY);
    EXPECT_EQ(queue.space(), actual_capacity - written);
    
    // Read exactly CAPACITY elements
    std::vector<int> read_data(CAPACITY);
    std::size_t read = queue.readN(read_data.data(), CAPACITY);
    EXPECT_EQ(read, CAPACITY);
    EXPECT_TRUE(queue.empty());
    
    // Verify data integrity
    EXPECT_EQ(read_data, write_data);
}

// Test bounds with different data sizes
TEST_F(SPSCQueueBoundsTest, DifferentDataSizeBounds) {
    // Test with larger data types
    const std::size_t CAPACITY = 256;
    
    // Test with double (8 bytes)
    {
        dro::SPSCQueue<double> double_queue(CAPACITY);
        std::vector<double> double_data(CAPACITY);
        std::iota(double_data.begin(), double_data.end(), 0.0);
        
        std::size_t written = double_queue.writeN(double_data.data(), CAPACITY);
        EXPECT_EQ(written, CAPACITY);
        
        std::vector<double> read_doubles(CAPACITY);
        std::size_t read = double_queue.readN(read_doubles.data(), CAPACITY);
        EXPECT_EQ(read, CAPACITY);
        EXPECT_EQ(read_doubles, double_data);
    }
    
    // Test with struct (larger size)
    struct LargeStruct {
        int a, b, c, d;
        double e, f;
        char padding[32];
        
        LargeStruct() : a(0), b(0), c(0), d(0), e(0.0), f(0.0) {
            std::memset(padding, 0, sizeof(padding));
        }
        
        LargeStruct(int val) : a(val), b(val+1), c(val+2), d(val+3), 
                               e(val+0.5), f(val+1.5) {
            std::memset(padding, static_cast<char>(val), sizeof(padding));
        }
        
        bool operator==(const LargeStruct& other) const {
            return a == other.a && b == other.b && c == other.c && d == other.d &&
                   e == other.e && f == other.f &&
                   std::memcmp(padding, other.padding, sizeof(padding)) == 0;
        }
    };
    
    {
        dro::SPSCQueue<LargeStruct> struct_queue(CAPACITY);
        std::vector<LargeStruct> struct_data;
        struct_data.reserve(CAPACITY);
        
        for (std::size_t i = 0; i < CAPACITY; ++i) {
            struct_data.emplace_back(static_cast<int>(i));
        }
        
        std::size_t written = struct_queue.writeN(struct_data.data(), CAPACITY);
        EXPECT_EQ(written, CAPACITY);
        
        std::vector<LargeStruct> read_structs(CAPACITY);
        std::size_t read = struct_queue.readN(read_structs.data(), CAPACITY);
        EXPECT_EQ(read, CAPACITY);
        
        for (std::size_t i = 0; i < CAPACITY; ++i) {
            EXPECT_EQ(read_structs[i], struct_data[i]) << "Struct mismatch at index " << i;
        }
    }
}

// Test wraparound bounds behavior
TEST_F(SPSCQueueBoundsTest, WrapAroundBounds) {
    const std::size_t CAPACITY = 128;
    dro::SPSCQueue<int> queue(CAPACITY);
    
    // Fill queue partially to create offset
    std::vector<int> offset_data(CAPACITY / 4);
    std::iota(offset_data.begin(), offset_data.end(), 1000);
    queue.writeN(offset_data.data(), offset_data.size());
    
    // Read it back to create offset for wraparound
    std::vector<int> temp(offset_data.size());
    queue.readN(temp.data(), temp.size());
    
    // Now write data that will definitely wrap around
    std::vector<int> wrap_data(CAPACITY);
    std::iota(wrap_data.begin(), wrap_data.end(), 0);
    
    // This should involve wraparound due to the offset
    std::size_t written = queue.writeN(wrap_data.data(), CAPACITY);
    EXPECT_EQ(written, CAPACITY);
    
    // Test peek behavior with wraparound
    const int* read_ptr1;
    const int* read_ptr2;
    std::size_t read_size1, read_size2;
    
    std::size_t available = queue.peek_read(read_ptr1, read_size1, read_ptr2, read_size2);
    EXPECT_EQ(available, CAPACITY);
    
    // Verify we can access all data across the wraparound boundary
    std::vector<int> peeked_data;
    peeked_data.reserve(CAPACITY);
    
    if (read_ptr1 && read_size1 > 0) {
        for (std::size_t i = 0; i < read_size1; ++i) {
            peeked_data.push_back(read_ptr1[i]);
        }
    }
    
    if (read_ptr2 && read_size2 > 0) {
        for (std::size_t i = 0; i < read_size2; ++i) {
            peeked_data.push_back(read_ptr2[i]);
        }
    }
    
    EXPECT_EQ(peeked_data.size(), CAPACITY);
    EXPECT_EQ(peeked_data, wrap_data);
    
    queue.commit_read(CAPACITY);
    EXPECT_TRUE(queue.empty());
}

// Test stack allocation bounds
TEST_F(SPSCQueueBoundsTest, StackAllocationBounds) {
    const std::size_t STACK_CAPACITY = 64;
    dro::SPSCQueue<int, STACK_CAPACITY> stack_queue(0);
    
    EXPECT_EQ(stack_queue.capacity(), STACK_CAPACITY);
    
    // Test full capacity operations on stack-allocated queue
    std::vector<int> test_data(STACK_CAPACITY);
    std::iota(test_data.begin(), test_data.end(), 0);
    
    std::size_t written = stack_queue.writeN(test_data.data(), STACK_CAPACITY);
    EXPECT_EQ(written, STACK_CAPACITY);
    
    // Test peek operations at full capacity
    int* write_ptr1;
    int* write_ptr2;
    std::size_t write_size1, write_size2;
    
    // Should return 0 space since queue is full
    std::size_t space = stack_queue.peek_write(write_ptr1, write_size1, write_ptr2, write_size2);
    EXPECT_EQ(space, 0);
    EXPECT_EQ(write_ptr1, nullptr);
    EXPECT_EQ(write_ptr2, nullptr);
    
    // Read back all data
    std::vector<int> read_data(STACK_CAPACITY);
    std::size_t read = stack_queue.readN(read_data.data(), STACK_CAPACITY);
    EXPECT_EQ(read, STACK_CAPACITY);
    EXPECT_EQ(read_data, test_data);
}

// Test extreme large capacity (heap allocation)
TEST_F(SPSCQueueBoundsTest, LargeCapacityBounds) {
    // Test with a reasonably large capacity
    const std::size_t LARGE_CAPACITY = 65536;  // 64K elements
    dro::SPSCQueue<int> large_queue(LARGE_CAPACITY);
    
    // With page alignment, actual capacity may be larger
    const std::size_t actual_capacity = large_queue.capacity();
    EXPECT_GE(actual_capacity, LARGE_CAPACITY);
    EXPECT_EQ(large_queue.space(), actual_capacity);
    
    // Test writing/reading in chunks to verify bounds handling
    const std::size_t CHUNK_SIZE = 4096;
    const std::size_t NUM_CHUNKS = LARGE_CAPACITY / CHUNK_SIZE;
    
    std::vector<int> chunk(CHUNK_SIZE);
    
    // Write in chunks
    for (std::size_t chunk_idx = 0; chunk_idx < NUM_CHUNKS; ++chunk_idx) {
        // Fill chunk with unique data
        std::iota(chunk.begin(), chunk.end(), static_cast<int>(chunk_idx * CHUNK_SIZE));
        
        std::size_t written = large_queue.writeN(chunk.data(), CHUNK_SIZE);
        EXPECT_EQ(written, CHUNK_SIZE) << "Failed to write chunk " << chunk_idx;
    }
    
    EXPECT_EQ(large_queue.size(), LARGE_CAPACITY);
    EXPECT_EQ(large_queue.space(), actual_capacity - LARGE_CAPACITY);
    
    // Read back in chunks and verify
    for (std::size_t chunk_idx = 0; chunk_idx < NUM_CHUNKS; ++chunk_idx) {
        std::vector<int> read_chunk(CHUNK_SIZE);
        std::size_t read = large_queue.readN(read_chunk.data(), CHUNK_SIZE);
        EXPECT_EQ(read, CHUNK_SIZE) << "Failed to read chunk " << chunk_idx;
        
        // Verify chunk data
        for (std::size_t i = 0; i < CHUNK_SIZE; ++i) {
            int expected = static_cast<int>(chunk_idx * CHUNK_SIZE + i);
            EXPECT_EQ(read_chunk[i], expected) 
                << "Data corruption in chunk " << chunk_idx << " at index " << i;
        }
    }
    
    EXPECT_TRUE(large_queue.empty());
}

// Test concurrent access bounds safety
TEST_F(SPSCQueueBoundsTest, ConcurrentBoundsSafety) {
    const std::size_t CAPACITY = 1024;
    const int NUM_ITERATIONS = 1000;
    dro::SPSCQueue<int> queue(CAPACITY);
    
    std::atomic<bool> producer_done{false};
    std::atomic<int> total_written{0};
    std::atomic<int> total_read{0};
    
    // Producer thread - uses peek/commit pattern
    std::thread producer([&]() {
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            int* write_ptr1;
            int* write_ptr2;
            std::size_t write_size1, write_size2;
            
            // Keep trying until we can write something
            while (true) {
                std::size_t space = queue.peek_write(write_ptr1, write_size1, write_ptr2, write_size2);
                if (space > 0) {
                    // Write up to 16 elements or available space
                    std::size_t to_write = std::min(space, std::size_t{16});
                    std::size_t written_in_chunks = 0;
                    
                    // Write to first chunk
                    if (write_ptr1 && write_size1 > 0) {
                        std::size_t chunk1_write = std::min(to_write, write_size1);
                        for (std::size_t i = 0; i < chunk1_write; ++i) {
                            write_ptr1[i] = total_written + static_cast<int>(i);
                        }
                        written_in_chunks += chunk1_write;
                    }
                    
                    // Write to second chunk if needed
                    if (written_in_chunks < to_write && write_ptr2 && write_size2 > 0) {
                        std::size_t chunk2_write = to_write - written_in_chunks;
                        for (std::size_t i = 0; i < chunk2_write; ++i) {
                            write_ptr2[i] = total_written + static_cast<int>(written_in_chunks + i);
                        }
                    }
                    
                    queue.commit_write(to_write);
                    total_written += static_cast<int>(to_write);
                    break;
                }
                std::this_thread::yield();
            }
        }
        producer_done = true;
    });
    
    // Consumer thread - uses readN
    std::thread consumer([&]() {
        std::vector<int> buffer(32);
        while (!producer_done || queue.size() > 0) {
            std::size_t read = queue.readN(buffer.data(), buffer.size());
            if (read > 0) {
                // Verify sequential data
                for (std::size_t i = 0; i < read; ++i) {
                    EXPECT_EQ(buffer[i], total_read + static_cast<int>(i))
                        << "Data corruption detected";
                }
                total_read += static_cast<int>(read);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    // Verify all data was transferred correctly
    EXPECT_EQ(total_written.load(), total_read.load());
    EXPECT_TRUE(queue.empty());
}

// Test memory alignment bounds
TEST_F(SPSCQueueBoundsTest, AlignmentBounds) {
    // Test with data types requiring specific alignment
    struct AlignedStruct {
        alignas(64) double data[8];  // Cache line aligned
        
        AlignedStruct() {
            for (int i = 0; i < 8; ++i) {
                data[i] = 0.0;
            }
        }
        
        AlignedStruct(double val) {
            for (int i = 0; i < 8; ++i) {
                data[i] = val + i;
            }
        }
        
        bool operator==(const AlignedStruct& other) const {
            for (int i = 0; i < 8; ++i) {
                if (data[i] != other.data[i]) return false;
            }
            return true;
        }
    };
    
    const std::size_t CAPACITY = 32;
    dro::SPSCQueue<AlignedStruct> aligned_queue(CAPACITY);
    
    // Verify we can write and read aligned structures without issues
    std::vector<AlignedStruct> test_structs;
    test_structs.reserve(CAPACITY);
    
    for (std::size_t i = 0; i < CAPACITY; ++i) {
        test_structs.emplace_back(static_cast<double>(i * 10));
    }
    
    std::size_t written = aligned_queue.writeN(test_structs.data(), CAPACITY);
    EXPECT_EQ(written, CAPACITY);
    
    std::vector<AlignedStruct> read_structs(CAPACITY);
    std::size_t read = aligned_queue.readN(read_structs.data(), CAPACITY);
    EXPECT_EQ(read, CAPACITY);
    
    for (std::size_t i = 0; i < CAPACITY; ++i) {
        EXPECT_EQ(read_structs[i], test_structs[i]) 
            << "Aligned struct mismatch at index " << i;
    }
}