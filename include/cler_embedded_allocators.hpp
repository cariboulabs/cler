#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <new>
#include <atomic>
#include <algorithm>

namespace cler {

// Thread-safe memory pool allocator for real-time systems
// Uses lock-free operations for multi-threaded safety
template<size_t BlockSize, size_t NumBlocks>
class ThreadSafePoolAllocator {
public:
    static constexpr size_t block_size = BlockSize;
    static constexpr size_t num_blocks = NumBlocks;
    
    template<typename T>
    struct rebind {
        using other = ThreadSafePoolAllocator<BlockSize, NumBlocks>;
    };
    
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    ThreadSafePoolAllocator() {
        // Initialize free list
        for (size_t i = 0; i < NumBlocks - 1; ++i) {
            *reinterpret_cast<size_t*>(&pool_[i * BlockSize]) = i + 1;
        }
        *reinterpret_cast<size_t*>(&pool_[(NumBlocks - 1) * BlockSize]) = NumBlocks;
    }
    
    // Make it copyable for STL allocator requirements, but reset state
    ThreadSafePoolAllocator(const ThreadSafePoolAllocator& other) : ThreadSafePoolAllocator() {
        (void)other; // Suppress unused parameter warning
    }
    
    ThreadSafePoolAllocator& operator=(const ThreadSafePoolAllocator& other) {
        (void)other; // Suppress unused parameter warning
        return *this;
    }
    
    template<typename T>
    T* allocate(std::size_t n) {
        if (n * sizeof(T) > BlockSize) {
            throw std::bad_alloc(); // Block too large
        }
        
        size_t expected = free_list_.load(std::memory_order_relaxed);
        
        while (expected < NumBlocks) {
            size_t next = *reinterpret_cast<size_t*>(&pool_[expected * BlockSize]);
            
            if (free_list_.compare_exchange_weak(expected, next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
                return reinterpret_cast<T*>(&pool_[expected * BlockSize]);
            }
        }
        
        throw std::bad_alloc(); // No free blocks
    }
    
    template<typename T>
    void deallocate(T* p, std::size_t) {
        if (!p) return;
        
        auto* block = reinterpret_cast<uint8_t*>(p);
        size_t index = (block - pool_.data()) / BlockSize;
        
        if (index >= NumBlocks) return; // Invalid pointer
        
        size_t expected = free_list_.load(std::memory_order_relaxed);
        
        do {
            *reinterpret_cast<size_t*>(block) = expected;
        } while (!free_list_.compare_exchange_weak(expected, index,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
    }
    
    bool operator==(const ThreadSafePoolAllocator&) const { return true; }
    bool operator!=(const ThreadSafePoolAllocator&) const { return false; }
    
private:
    alignas(std::max_align_t) std::array<uint8_t, BlockSize * NumBlocks> pool_;
    std::atomic<size_t> free_list_{0};
};

// Static memory pool allocator for embedded systems (single-threaded)
// Pre-allocates a fixed pool of memory at compile time
template<size_t PoolSize, size_t Alignment = alignof(std::max_align_t)>
class StaticPoolAllocator {
public:
    template<typename T>
    struct rebind {
        using other = StaticPoolAllocator<PoolSize, Alignment>;
    };
    
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    StaticPoolAllocator() = default;
    
    template<typename U>
    StaticPoolAllocator(const StaticPoolAllocator<PoolSize, Alignment>&) : StaticPoolAllocator() {}
    
    template<typename T>
    T* allocate(std::size_t n) {
        const std::size_t bytes_needed = n * sizeof(T);
        const std::size_t aligned_size = align_up(bytes_needed, Alignment);
        
        if (offset_ + aligned_size > PoolSize) {
            throw std::bad_alloc(); // Pool exhausted
        }
        
        void* ptr = pool_.data() + offset_;
        offset_ += aligned_size;
        
        return static_cast<T*>(ptr);
    }
    
    template<typename T>
    void deallocate(T*, std::size_t) {
        // Static pool doesn't support individual deallocation
        // Memory is reclaimed when allocator is destroyed
    }
    
    void reset() {
        offset_ = 0; // Reset pool for reuse
    }
    
    std::size_t bytes_used() const { return offset_; }
    std::size_t bytes_available() const { return PoolSize - offset_; }
    
    bool operator==(const StaticPoolAllocator&) const { return true; }
    bool operator!=(const StaticPoolAllocator&) const { return false; }
    
private:
    static std::size_t align_up(std::size_t size, std::size_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
    
    alignas(Alignment) std::array<char, PoolSize> pool_;
    std::size_t offset_ = 0;
};

// Advanced stack allocator with marker-based unwinding
// Suitable for temporary allocations with LIFO pattern and scope-based cleanup
template<size_t BufferSize>
class StackAllocator {
public:
    struct Marker {
        size_t offset;
    };
    
    template<typename T>
    struct rebind {
        using other = StackAllocator<BufferSize>;
    };
    
    using value_type = char;
    
    StackAllocator() = default;
    
    template<typename U>
    StackAllocator(const StackAllocator<BufferSize>&) : StackAllocator() {}
    
    template<typename T>
    T* allocate(std::size_t n) {
        const std::size_t bytes_needed = n * sizeof(T);
        const std::size_t aligned_size = align_up(bytes_needed, alignof(T));
        
        if (top_ + aligned_size > BufferSize) {
            throw std::bad_alloc(); // Stack overflow
        }
        
        void* ptr = buffer_.data() + top_;
        top_ += aligned_size;
        
        return static_cast<T*>(ptr);
    }
    
    template<typename T>
    void deallocate(T* ptr, std::size_t n) {
        // Stack allocator only supports LIFO deallocation
        const std::size_t bytes = n * sizeof(T);
        const std::size_t aligned_size = align_up(bytes, alignof(T));
        
        // Check if this is the most recent allocation
        if (reinterpret_cast<char*>(ptr) + aligned_size == buffer_.data() + top_) {
            top_ -= aligned_size; // Pop from stack
        }
        // Otherwise, memory will be reclaimed when newer allocations are freed
    }
    
    // Advanced marker-based operations for scope management
    Marker get_marker() const {
        return {top_};
    }
    
    void free_to_marker(Marker marker) {
        top_ = marker.offset;
    }
    
    void reset() {
        top_ = 0; // Reset entire stack
    }
    
    size_t used() const { return top_; }
    size_t available() const { return BufferSize - top_; }
    
    bool operator==(const StackAllocator&) const { return true; }
    bool operator!=(const StackAllocator&) const { return false; }
    
private:
    static std::size_t align_up(std::size_t size, std::size_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
    
    alignas(std::max_align_t) std::array<char, BufferSize> buffer_;
    std::size_t top_ = 0;
};

// Pre-allocated memory region allocator
// Uses a user-provided memory region
template<typename T>
class RegionAllocator {
public:
    using value_type = T;
    
    RegionAllocator(T* memory, std::size_t count) 
        : memory_(memory), size_(count), offset_(0) {}
    
    template<typename U>
    RegionAllocator(const RegionAllocator<U>& other)
        : memory_(reinterpret_cast<T*>(other.memory_))
        , size_(other.size_ * sizeof(U) / sizeof(T))
        , offset_(other.offset_ * sizeof(U) / sizeof(T)) {}
    
    T* allocate(std::size_t n) {
        if (offset_ + n > size_) {
            throw std::bad_alloc(); // Region exhausted
        }
        
        T* ptr = memory_ + offset_;
        offset_ += n;
        return ptr;
    }
    
    void deallocate(T*, std::size_t) {
        // Region allocator doesn't support individual deallocation
    }
    
    void reset() {
        offset_ = 0; // Reset region for reuse
    }
    
    template<typename U>
    struct rebind {
        using other = RegionAllocator<U>;
    };
    
    bool operator==(const RegionAllocator& other) const { 
        return memory_ == other.memory_; 
    }
    bool operator!=(const RegionAllocator& other) const { 
        return !(*this == other); 
    }
    
private:
    template<typename U> friend class RegionAllocator;
    
    T* memory_;
    std::size_t size_;
    std::size_t offset_;
};

// Allocator traits for compile-time detection and optimization
template<typename Alloc>
struct is_static_allocator : std::false_type {};

template<size_t PoolSize, size_t Alignment>
struct is_static_allocator<StaticPoolAllocator<PoolSize, Alignment>> : std::true_type {};

template<size_t BlockSize, size_t NumBlocks>
struct is_static_allocator<ThreadSafePoolAllocator<BlockSize, NumBlocks>> : std::true_type {};

template<size_t BufferSize>
struct is_static_allocator<StackAllocator<BufferSize>> : std::true_type {};

template<typename T>
struct is_static_allocator<RegionAllocator<T>> : std::true_type {};

// Helper to detect if an allocator provides thread-safe operations
template<typename Alloc>
struct is_thread_safe_allocator : std::false_type {};

template<size_t BlockSize, size_t NumBlocks>
struct is_thread_safe_allocator<ThreadSafePoolAllocator<BlockSize, NumBlocks>> : std::true_type {};

// Helper to detect if an allocator supports markers/scoped unwinding
template<typename Alloc>
struct supports_markers : std::false_type {};

template<size_t BufferSize>
struct supports_markers<StackAllocator<BufferSize>> : std::true_type {};

} // namespace cler