#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <new>
#include <atomic>
#include <algorithm>

namespace cler {

// Memory pool allocator for real-time systems
template<size_t BlockSize, size_t NumBlocks>
class MemoryPoolAllocator {
public:
    static constexpr size_t block_size = BlockSize;
    static constexpr size_t num_blocks = NumBlocks;
    
    MemoryPoolAllocator() {
        // Initialize free list
        for (size_t i = 0; i < NumBlocks - 1; ++i) {
            *reinterpret_cast<size_t*>(&pool_[i * BlockSize]) = i + 1;
        }
        *reinterpret_cast<size_t*>(&pool_[(NumBlocks - 1) * BlockSize]) = NumBlocks;
    }
    
    void* allocate(size_t n) {
        if (n > BlockSize) {
            return nullptr;  // Block too large
        }
        
        size_t expected = free_list_.load(std::memory_order_relaxed);
        
        while (expected < NumBlocks) {
            size_t next = *reinterpret_cast<size_t*>(&pool_[expected * BlockSize]);
            
            if (free_list_.compare_exchange_weak(expected, next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
                return &pool_[expected * BlockSize];
            }
        }
        
        return nullptr;  // No free blocks
    }
    
    void deallocate(void* p, size_t) {
        if (!p) return;
        
        auto* block = static_cast<uint8_t*>(p);
        size_t index = (block - pool_.data()) / BlockSize;
        
        if (index >= NumBlocks) return;  // Invalid pointer
        
        size_t expected = free_list_.load(std::memory_order_relaxed);
        
        do {
            *reinterpret_cast<size_t*>(block) = expected;
        } while (!free_list_.compare_exchange_weak(expected, index,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
    }
    
private:
    alignas(std::max_align_t) std::array<uint8_t, BlockSize * NumBlocks> pool_;
    std::atomic<size_t> free_list_{0};
};

// STL-compatible allocator wrapper
template<typename T, size_t BlockSize, size_t NumBlocks>
class PoolAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    PoolAllocator() = default;
    
    template<typename U>
    PoolAllocator(const PoolAllocator<U, BlockSize, NumBlocks>&) {}
    
    T* allocate(size_type n) {
        if (n > BlockSize / sizeof(T)) {
            throw std::bad_alloc();
        }
        
        void* p = pool_.allocate(n * sizeof(T));
        if (!p) {
            throw std::bad_alloc();
        }
        
        return static_cast<T*>(p);
    }
    
    void deallocate(T* p, size_type n) {
        pool_.deallocate(p, n * sizeof(T));
    }
    
    template<typename U>
    struct rebind {
        using other = PoolAllocator<U, BlockSize, NumBlocks>;
    };
    
    bool operator==(const PoolAllocator&) const { return true; }
    bool operator!=(const PoolAllocator&) const { return false; }
    
private:
    static MemoryPoolAllocator<BlockSize, NumBlocks> pool_;
};

template<typename T, size_t BlockSize, size_t NumBlocks>
MemoryPoolAllocator<BlockSize, NumBlocks> PoolAllocator<T, BlockSize, NumBlocks>::pool_;

// Linear allocator for temporary allocations
template<size_t Size>
class LinearAllocator {
public:
    LinearAllocator() = default;
    
    void* allocate(size_t n, size_t alignment = alignof(std::max_align_t)) {
        // Align the current position
        size_t space = Size - offset_;
        void* ptr = buffer_.data() + offset_;
        
        if (std::align(alignment, n, ptr, space)) {
            offset_ = static_cast<uint8_t*>(ptr) - buffer_.data() + n;
            return ptr;
        }
        
        return nullptr;  // Not enough space
    }
    
    void reset() {
        offset_ = 0;
    }
    
    size_t used() const { return offset_; }
    size_t available() const { return Size - offset_; }
    
private:
    alignas(std::max_align_t) std::array<uint8_t, Size> buffer_;
    size_t offset_ = 0;
};

// Stack allocator with LIFO deallocation
template<size_t Size>
class StackAllocator {
public:
    struct Marker {
        size_t offset;
    };
    
    StackAllocator() = default;
    
    void* allocate(size_t n, size_t alignment = alignof(std::max_align_t)) {
        // Align the current position
        size_t space = Size - offset_;
        void* ptr = buffer_.data() + offset_;
        
        if (std::align(alignment, n, ptr, space)) {
            offset_ = static_cast<uint8_t*>(ptr) - buffer_.data() + n;
            return ptr;
        }
        
        return nullptr;  // Not enough space
    }
    
    Marker get_marker() const {
        return {offset_};
    }
    
    void free_to_marker(Marker marker) {
        offset_ = marker.offset;
    }
    
    void reset() {
        offset_ = 0;
    }
    
private:
    alignas(std::max_align_t) std::array<uint8_t, Size> buffer_;
    size_t offset_ = 0;
};

// Allocator traits for compile-time selection
template<typename Alloc>
struct is_static_allocator : std::false_type {};

template<typename T, size_t BlockSize, size_t NumBlocks>
struct is_static_allocator<PoolAllocator<T, BlockSize, NumBlocks>> : std::true_type {};

template<size_t Size>
struct is_static_allocator<LinearAllocator<Size>> : std::true_type {};

template<size_t Size>
struct is_static_allocator<StackAllocator<Size>> : std::true_type {};

} // namespace cler