# Doubly Mapped Buffer Implementation Plan V3 for Cler

## Executive Summary

This plan adds doubly mapped circular buffers to Cler's SPSC queue to eliminate the two-pointer read pattern in file I/O operations. The implementation uses shared memory (via `memfd_create` or `shm_open`) to create two virtual mappings of the same physical memory, providing a contiguous view of circular buffer data. This is particularly beneficial for SDR applications where buffer sizes are typically 32KB or larger.

## 1. Problem Analysis

### Current Issue
- `peek_read()` returns two pointers when buffer wraps: `(ptr1, size1)` and `(ptr2, size2)`
- File sink must call `fwrite()` twice for wrapped data
- Small `size1` values lead to inefficient I/O patterns
- Poor kernel buffer utilization

### Performance Impact
- Extra system calls (2x `fwrite` instead of 1)
- Small writes don't leverage kernel buffering
- Increased context switching overhead
- Reduced throughput for high-frequency small writes
- Critical for SDR applications with continuous high-bandwidth streams

## 2. Solution: Doubly Mapped Circular Buffer

### Concept
```
Physical Memory: [buffer: N bytes]
Virtual Memory:  [mapping1: N bytes][mapping2: N bytes]
                 ^-- same physical memory mapped twice --^

When reading at position near end:
Traditional: ptr1 → [end portion] + ptr2 → [beginning portion]  
Doubly Mapped: ptr → [end portion][beginning portion] (contiguous!)
```

### Key Benefits
- Always return single contiguous pointer for reads
- Single `fwrite()` call regardless of buffer position
- Better I/O performance
- Transparent fallback when not available
- Particularly beneficial for SDR applications with 32KB+ buffers

## 3. Implementation Approach

### 3.1 Constraints

- **Only works with heap buffers** (N=0): Stack buffers cannot be remapped
- **Minimum buffer size**: 32KB (typical SDR buffer size)
- **Platform support**: Linux, macOS, FreeBSD, embedded Linux
- **No API changes**: Existing code continues to work

### 3.2 File Organization

```
include/
├── cler_spsc-queue.hpp          # Main SPSC queue
├── virtual_memory/              # New directory
│   ├── cler_vmem_posix.hpp     # Linux/macOS/BSD implementation
│   └── cler_vmem_none.hpp      # Fallback (no mmap support)
└── cler_platform.hpp            # Platform detection (updated)
```

### 3.3 Buffer Implementation

```cpp
namespace dro {
namespace details {

// Enhanced heap buffer that tries doubly mapped first
template <typename T, typename Allocator = std::allocator<T>>
struct AdaptiveHeapBuffer {
    const std::size_t capacity_;
    T* buffer_;
    [[no_unique_address]] Allocator allocator_;
    bool is_doubly_mapped_ = false;
    
    // Platform-specific members
    #if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    int shm_fd_ = -1;
    void* mmap_base_ = nullptr;
    size_t mmap_size_ = 0;
    #endif
    
    explicit AdaptiveHeapBuffer(std::size_t capacity, const Allocator& allocator);
    ~AdaptiveHeapBuffer();
};

// Stack buffer remains unchanged - cannot be doubly mapped
template <typename T, std::size_t N, typename Allocator = std::allocator<T>>
struct StackBuffer {
    // ... existing implementation unchanged ...
};

}
}
```

## 4. Implementation Details

### 4.1 Platform Detection

```cpp
// In cler_platform.hpp
namespace cler {
namespace platform {

// Runtime capability check
inline bool supports_doubly_mapped_buffers() {
    #if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
        static bool tested = false;
        static bool supported = false;
        
        if (!tested) {
            tested = true;
            // Runtime test with small allocation
            supported = test_doubly_mapped_creation();
        }
        return supported;
    #else
        return false;
    #endif
}

}
}
```

### 4.2 AdaptiveHeapBuffer Implementation

```cpp
explicit AdaptiveHeapBuffer(std::size_t capacity, const Allocator& allocator) 
    : capacity_(capacity + 1), allocator_(allocator) {
    
    const size_t buffer_bytes = capacity_ * sizeof(T);
    
    // Only try doubly mapped for buffers ≥32KB (typical SDR buffer size)
    if (cler::platform::supports_doubly_mapped_buffers() && buffer_bytes >= 32768) {
        if (try_create_doubly_mapped(buffer_bytes)) {
            return;  // Success
        }
    }
    
    // Fallback to standard heap allocation
    const size_t total_size = capacity_ + (2 * padding);
    buffer_ = allocator_.allocate(total_size);
    // ... initialize elements ...
}

bool try_create_doubly_mapped(size_t buffer_bytes) {
    const size_t page_size = sysconf(_SC_PAGESIZE);
    const size_t aligned_size = ((buffer_bytes + page_size - 1) / page_size) * page_size;
    
    // Create shared memory
    #ifdef __linux__
        #ifdef __NR_memfd_create
            shm_fd_ = syscall(__NR_memfd_create, "cler_buffer", MFD_CLOEXEC);
        #endif
        if (shm_fd_ == -1) {
            // Fallback to POSIX shm
            shm_fd_ = shm_open("/cler_buffer", O_CREAT | O_RDWR | O_EXCL, 0600);
            if (shm_fd_ != -1) shm_unlink("/cler_buffer");
        }
    #else  // macOS, FreeBSD
        char name[64];
        snprintf(name, sizeof(name), "/cler_%d_%p", getpid(), this);
        shm_fd_ = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (shm_fd_ != -1) shm_unlink(name);
    #endif
    
    if (shm_fd_ == -1) return false;
    
    // Set size
    if (ftruncate(shm_fd_, aligned_size) == -1) {
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    // Reserve address space
    void* addr = mmap(nullptr, aligned_size * 2, PROT_NONE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    // Map shared memory twice
    void* m1 = mmap(addr, aligned_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_FIXED, shm_fd_, 0);
    void* m2 = mmap((char*)addr + aligned_size, aligned_size, 
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shm_fd_, 0);
    
    if (m1 == MAP_FAILED || m2 == MAP_FAILED) {
        munmap(addr, aligned_size * 2);
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    mmap_base_ = addr;
    mmap_size_ = aligned_size;
    buffer_ = static_cast<T*>(m1);
    is_doubly_mapped_ = true;
    
    // Initialize elements
    if constexpr (!std::is_trivially_constructible_v<T>) {
        for (size_t i = 0; i < capacity_; ++i) {
            new (buffer_ + i) T();
        }
    }
    
    return true;
}
```

### 4.3 SPSCQueue Changes

```cpp
template <typename T, std::size_t N = 0, typename Allocator = std::allocator<T>>
class SPSCQueue : public std::conditional_t<N == 0, 
                                           details::AdaptiveHeapBuffer<T, Allocator>,
                                           details::StackBuffer<T, N>> {
public:
    // All existing methods remain unchanged
    
    // NEW: Zero-copy read (only available with doubly mapped heap buffers)
    std::pair<const T*, std::size_t> read_span() noexcept {
        if constexpr (N == 0) {
            // Only heap buffers can be doubly mapped
            if (this->is_doubly_mapped_) {
                const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);
                auto writeIndexCache = writer_.writeIndex_.load(std::memory_order_acquire);
                reader_.writeIndexCache_ = writeIndexCache;
                
                std::size_t available;
                if (writeIndexCache >= readIndex) {
                    available = writeIndexCache - readIndex;
                } else {
                    available = this->capacity_ - readIndex + writeIndexCache;
                }
                
                if (available == 0) {
                    return {nullptr, 0};
                }
                
                // With doubly mapped, ALL data is contiguous
                const T* ptr = &this->buffer_[readIndex + base_type::padding];
                return {ptr, available};
            }
        }
        // Stack buffers or non-doubly-mapped heap buffers
        return {nullptr, 0};
    }
    
    // Keep peek_read UNCHANGED - always returns two pointers
    std::size_t peek_read(const T*& ptr1, std::size_t& size1,
                         const T*& ptr2, std::size_t& size2) noexcept {
        // ... existing implementation unchanged ...
    }
    
    // REMOVE these methods - they're never used:
    // - peek_write() 
    // - commit_write()
};
```

## 5. Platform Support

| Platform | Support | Implementation | Notes |
|----------|---------|----------------|-------|
| Linux x64/ARM | Yes | memfd_create or shm_open | Kernel 3.17+ for memfd |
| Embedded Linux | Yes | Same as Linux | Works on Raspberry Pi, i.MX, etc. |
| macOS | Yes | shm_open | All versions |
| FreeBSD | Yes | shm_open | All versions |
| Android | Yes | shm_open or ashmem | API 26+ recommended |
| Windows | No | N/A | Future work |
| RTOS | No | N/A | Future work |

## 6. File Sink Integration

```cpp
template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in;  // No changes to declaration
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        // Try zero-copy path first
        auto [ptr, size] = in.read_span();
        if (ptr && size > 0) {
            // Single write, no copy!
            size_t written = std::fwrite(ptr, sizeof(T), size, _fp);
            if (written != size) return cler::Error::TERM_IOError;
            in.commit_read(written);
            return cler::Empty{};
        }
        
        // Fall back to standard peek_read (existing code)
        const T* ptr1 = nullptr;
        const T* ptr2 = nullptr;
        size_t sz1 = 0, sz2 = 0;
        size_t available = in.peek_read(ptr1, sz1, ptr2, sz2);
        
        if (available == 0) {
            std::fflush(_fp);
            return cler::Error::NotEnoughSamples;
        }
        
        if (sz1 > 0) {
            size_t written = std::fwrite(ptr1, sizeof(T), sz1, _fp);
            if (written != sz1) return cler::Error::TERM_IOError;
        }
        
        if (sz2 > 0) {
            size_t written = std::fwrite(ptr2, sizeof(T), sz2, _fp);
            if (written != sz2) return cler::Error::TERM_IOError;
        }
        
        in.commit_read(sz1 + sz2);
        return cler::Empty{};
    }
};
```

## 7. Testing Strategy

### 7.1 Unit Tests

```cpp
TEST(SPSCQueue, DoublyMappedOnlyHeapBuffers) {
    // Stack buffer - read_span should always return null
    SPSCQueue<float, 1024> stack_queue;
    auto [ptr, size] = stack_queue.read_span();
    ASSERT_EQ(ptr, nullptr);
    ASSERT_EQ(size, 0);
    
    // Heap buffer - may or may not be doubly mapped
    SPSCQueue<float> heap_queue(8192);  // 32KB for float
    // Test works regardless of doubly mapped status
}

TEST(SPSCQueue, LargeBufferDoublyMapped) {
    // Large buffer should trigger doubly mapped on supported platforms
    SPSCQueue<float> queue(16384);  // 64KB
    
    // Fill and wrap
    for (int i = 0; i < 16000; i++) {
        queue.push(i * 0.1f);
    }
    
    float val;
    for (int i = 0; i < 8000; i++) {
        queue.pop(val);
    }
    
    for (int i = 16000; i < 20000; i++) {
        queue.push(i * 0.1f);
    }
    
    // Test both APIs
    auto [span_ptr, span_size] = queue.read_span();
    const float* p1, *p2;
    size_t s1, s2;
    size_t total = queue.peek_read(p1, s1, p2, s2);
    
    if (span_ptr) {
        // Doubly mapped: single contiguous span
        ASSERT_EQ(span_size, total);
        // Verify data integrity
    } else {
        // Not doubly mapped: two pointers
        ASSERT_EQ(s1 + s2, total);
    }
}

TEST(SPSCQueue, SDRBufferSizes) {
    // Test typical SDR buffer sizes
    const size_t sample_rates[] = {2048000, 10000000, 20000000};  // 2.048, 10, 20 MSPS
    const size_t buffer_ms = 10;  // 10ms buffers
    
    for (size_t rate : sample_rates) {
        size_t buffer_size = (rate * buffer_ms) / 1000;
        SPSCQueue<std::complex<float>> queue(buffer_size);
        
        // Verify construction succeeded
        ASSERT_EQ(queue.capacity(), buffer_size);
    }
}
```

### 7.2 Performance Benchmarks

```cpp
void benchmark_sdr_file_write() {
    const size_t buffer_size = 8192;  // 32KB for float
    const size_t total_samples = 100'000'000;  // 400MB
    
    SPSCQueue<float> queue(buffer_size);
    
    // Producer thread
    std::thread producer([&] {
        for (size_t i = 0; i < total_samples; i++) {
            queue.push(i * 0.1f);
        }
    });
    
    // Consumer with file write
    FILE* fp = fopen("test.bin", "wb");
    auto start = std::chrono::steady_clock::now();
    size_t total_written = 0;
    size_t write_calls = 0;
    
    while (total_written < total_samples) {
        auto [ptr, size] = queue.read_span();
        if (ptr && size > 0) {
            fwrite(ptr, sizeof(float), size, fp);
            queue.commit_read(size);
            total_written += size;
            write_calls++;
        } else {
            // Fallback path
            // ... existing peek_read code ...
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    
    // Report metrics:
    // - Throughput (MB/s)
    // - Average write size
    // - System calls per MB
}
```

## 8. Implementation Timeline

### Week 1: Core Implementation
- Implement AdaptiveHeapBuffer with doubly mapped support
- Add platform detection to cler_platform.hpp
- Create virtual_memory subdirectory structure
- Basic unit tests

### Week 2: Integration
- Add read_span() method to SPSCQueue
- Remove peek_write/commit_write methods
- Test on Linux and macOS
- Verify fallback behavior

### Week 3: SDR Testing
- Performance benchmarks with SDR-sized buffers
- Test with GNU Radio file sinks
- Embedded Linux testing (Raspberry Pi, etc.)
- Stress testing with continuous streams

### Week 4: Documentation and Polish
- API documentation
- Example programs for SDR use cases
- Performance tuning guide
- Release notes

## 9. Success Criteria

- ✓ Zero API changes for existing code
- ✓ Transparent fallback on all platforms
- ✓ read_span() returns null for stack buffers
- ✓ 32KB minimum size for doubly mapped attempt
- ✓ 10-30% throughput improvement for file I/O
- ✓ 50% reduction in write calls for wrapped buffers
- ✓ No performance regression
- ✓ Works on embedded Linux (Raspberry Pi, etc.)

## 10. Future Enhancements (Not in V3)

- Huge page support for very large buffers (≥1MB)
- Windows support via CreateFileMapping/MapViewOfFile
- RTOS support with custom memory managers
- Memory locking options for real-time
- NUMA awareness for multi-socket systems
- io_uring integration for zero-copy I/O

## 11. API Summary

```cpp
// NEW: Zero-copy read (returns {nullptr, 0} if not available)
std::pair<const T*, size_t> read_span() noexcept;
std::pair<T*, size_t> write_span() noexcept;


// EXISTING: Unchanged methods
size_t peek_read(const T*& ptr1, size_t& size1, 
                const T*& ptr2, size_t& size2) noexcept;
void commit_read(size_t count) noexcept;
size_t writeN(const T* src, size_t count) noexcept;
size_t readN(T* dst, size_t count) noexcept;

// REMOVED: Never used in practice
// - peek_write()
// - commit_write()
```

## 12. Key Design Decisions

1. **Only heap buffers**: Stack buffers cannot be doubly mapped
2. **32KB threshold**: Appropriate for SDR applications
3. **Single new method**: Only add read_span()
4. **Remove unused APIs**: Delete peek_write/commit_write
5. **Transparent fallback**: Returns {nullptr, 0} when unavailable
6. **No exceptions**: All failures handled gracefully
7. **Embedded Linux support**: Same code works on RPi, i.MX, etc.

## 13. Notes for Implementers

- AdaptiveHeapBuffer replaces HeapBuffer when N=0
- StackBuffer remains completely unchanged
- The 32KB threshold can be tuned based on testing
- Consider adding huge page support in V4 for ≥1MB buffers
- File structure allows future platform additions without breaking changes

## 14. Implementation Notes (Post-Completion)

### Critical Architecture Decisions Made:
1. **Padding Strategy**: Handle at allocation time, not access
   - Doubly mapped: No padding (page-aligned)
   - Standard heap: Allocate+padding, adjust buffer_ pointer  
   - Stack: Padding via get_buffer_ptr() at access

2. **Buffer Management**: 
   - raw_allocation_ for cleanup, buffer_ for usage
   - get_buffer_ptr() centralizes access logic

3. **Method Cleanup**: Removed unused force_* and commit_write methods

### Platform Error Handling:
- cler_vmem_none.hpp throws exceptions (not silent failure)
- Clear user guidance to supported platforms

### Performance Confirmation:
- File I/O: 1 write call (vs multiple small writes)
- All tests passing, backward compatible
- CaribouLite integration confirmed working

## 15. Success Criteria ✅ ACHIEVED:
- [✅] Zero-copy file I/O (1 write vs multiple)
- [✅] Platform detection working  
- [✅] Transparent fallback functional
- [✅] All existing tests pass
- [✅] Source blocks (peek_write) work
- [✅] 32KB threshold appropriate for SDR
- [✅] Exception-based error handling for unsupported platforms
- [✅] Clean method interface (removed unused methods)
- [✅] Centralized buffer access logic