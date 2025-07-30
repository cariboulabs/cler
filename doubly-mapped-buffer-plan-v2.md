# Doubly Mapped Buffer Implementation Plan V2 for Cler

## Executive Summary

This plan outlines adding doubly mapped circular buffers to Cler's SPSC queue to eliminate the two-pointer read pattern in file I/O operations. The key insight is using shared memory (via `memfd_create` or `shm_open`) to create two virtual mappings of the same physical memory, providing a contiguous view of circular buffer data.

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
- Simplified consumer code

## 3. Architecture Design

### 3.1 Buffer Strategy Pattern

```cpp
namespace dro {

enum class BufferStrategy {
    Auto,          // Automatically choose best available
    Standard,      // Force standard heap/stack buffer
    DoublyMapped   // Force doubly mapped (will throw if unavailable)
};

// Buffer capabilities interface
struct BufferCapabilities {
    bool supports_contiguous_read;
    bool is_doubly_mapped;
    size_t alignment_requirement;
    size_t overhead_bytes;
};

}
```

### 3.2 Three-Layer Implementation

#### Layer 1: Buffer Implementations
- `HeapBuffer` - Standard heap allocation (existing)
- `StackBuffer` - Stack allocation (existing)
- `DoublyMappedBuffer` - New shared memory implementation

#### Layer 2: SPSC Queue Variants
- `SPSCQueue` - Existing implementation
- `SPSCQueueAdaptive` - New adaptive implementation with runtime buffer selection

#### Layer 3: Channel Interface
- Extend `ChannelBase` with optional `peek_read_contiguous()`
- Maintain backward compatibility
- Runtime capability detection

### 3.3 Platform Abstraction

```cpp
namespace cler {
namespace platform {

// Feature detection result
struct DoublyMappedSupport {
    bool compile_time_capable;    // Platform could support it
    bool runtime_available;       // Actually works at runtime
    bool has_mmu;                // MMU present
    bool has_posix_mmap;         // POSIX mmap available
    bool has_memfd_create;       // Linux memfd_create available
    bool has_shm_open;           // POSIX shm_open available
    size_t page_size;            // System page size
    const char* failure_reason;  // Why not supported
};

DoublyMappedSupport detect_doubly_mapped_support();

}
}
```

## 4. Implementation Details

### 4.1 DoublyMappedBuffer Implementation

```cpp
template <typename T, typename Allocator = std::allocator<T>>
struct DoublyMappedBuffer {
    // Core members
    const std::size_t capacity_;     // Logical capacity
    T* buffer_;                      // First mapping base
    T* buffer_second_mapping_;       // Second mapping base
    void* mmap_base_;               // mmap allocation base
    std::size_t mmap_size_;         // Size of each mapping
    int shm_fd_;                    // Shared memory file descriptor
    
    // Implementation approach:
    // 1. Create shared memory via memfd_create (Linux) or shm_open (POSIX)
    // 2. Resize to page-aligned buffer size
    // 3. Reserve 2x address space with PROT_NONE
    // 4. Map shared memory twice at consecutive addresses
    // 5. Verify mappings work correctly
    
    // Key methods:
    T* get_contiguous_ptr(size_t offset, size_t count);
    bool validate_mappings();
    void prefault_pages();  // Optional optimization
};
```

### 4.2 Adaptive SPSC Queue

```cpp
template <typename T, size_t N = 0, BufferStrategy Strategy = BufferStrategy::Auto>
class SPSCQueueAdaptive {
private:
    // Runtime polymorphic buffer (only when needed)
    struct BufferConcept {
        virtual ~BufferConcept() = default;
        virtual T* data() = 0;
        virtual size_t capacity() const = 0;
        virtual BufferCapabilities capabilities() const = 0;
        virtual T* get_contiguous_read_ptr(size_t offset, size_t count) = 0;
    };
    
    // Type-erased buffer holder
    std::unique_ptr<BufferConcept> buffer_;
    
public:
    // New unified peek_read that adapts to buffer type
    size_t peek_read(const T*& ptr1, size_t& size1, 
                     const T*& ptr2, size_t& size2);
    
    // New method for guaranteed contiguous read
    std::pair<const T*, size_t> peek_read_contiguous();
    
    // Query capabilities
    BufferCapabilities get_buffer_capabilities() const;
};
```

### 4.3 Fallback Strategy

```
1. Try DoublyMappedBuffer (if strategy allows)
   ├─ Success → Use optimized path
   └─ Failure → Log reason, try next
   
2. Try HeapBuffer 
   ├─ Success → Use standard path
   └─ Failure → Throw exception

3. For StackBuffer (N > 0)
   └─ Always use standard path (no doubly mapped for stack)
```

## 5. Platform Support Matrix

| Platform | MMU | mmap | memfd | shm_open | Support | Notes |
|----------|-----|------|-------|----------|---------|-------|
| Linux x64 | ✓ | ✓ | ✓* | ✓ | Full | *Kernel 3.17+ |
| Linux ARM | ✓ | ✓ | ✓* | ✓ | Full | *Kernel 3.17+ |
| macOS | ✓ | ✓ | ✗ | ✓ | Full | Use shm_open |
| FreeBSD | ✓ | ✓ | ✗ | ✓ | Full | Use shm_open |
| Android | ✓ | ✓ | ✓* | ✓ | Full | *API 30+ |
| Windows | ✓ | ✗ | ✗ | ✗ | None* | *Could use MapViewOfFile |
| QNX | ✓ | ✓ | ✗ | ✓ | Full | Use shm_open |
| VxWorks | ?† | ✓ | ✗ | ✓ | Conditional | †Config dependent |
| Zephyr | ✗ | ✗ | ✗ | ✗ | None | Embedded RTOS |
| Bare Metal | ✗ | ✗ | ✗ | ✗ | None | No OS support |

## 6. API Design

### 6.1 Channel Interface Extension

```cpp
template <typename T>
struct ChannelBase {
    // Existing virtual methods...
    
    // New optional methods with default implementations
    virtual std::pair<const T*, size_t> peek_read_contiguous() {
        // Default: return first segment only
        const T* ptr1, *ptr2;
        size_t size1, size2;
        peek_read(ptr1, size1, ptr2, size2);
        return {ptr1, size1};
    }
    
    virtual BufferCapabilities get_capabilities() const {
        return {false, false, alignof(T), 0};
    }
};
```

### 6.2 User-Facing API

```cpp
// Option 1: Explicit channel type
using FastFileChannel = cler::Channel<float, 0, BufferStrategy::DoublyMapped>;

// Option 2: Auto-detecting channel
using AdaptiveChannel = cler::Channel<float, 0, BufferStrategy::Auto>;

// Option 3: Runtime configuration
cler::ChannelConfig config;
config.buffer_strategy = BufferStrategy::Auto;
config.fallback_on_error = true;
auto channel = cler::make_channel<float>(1024, config);
```

## 7. File Sink Integration

### 7.1 Adaptive Implementation

```cpp
template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    using ChannelType = cler::Channel<T, 0, BufferStrategy::Auto>;
    ChannelType in;
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        // Check capabilities once
        static const bool can_do_contiguous = 
            in.get_capabilities().supports_contiguous_read;
        
        if (can_do_contiguous) {
            // Fast path: single fwrite
            auto [ptr, size] = in.peek_read_contiguous();
            if (size > 0) {
                size_t written = std::fwrite(ptr, sizeof(T), size, _fp);
                // Handle errors...
                in.commit_read(written);
            }
        } else {
            // Fallback: existing two-pointer implementation
            // ... existing code ...
        }
        
        return cler::Empty{};
    }
};
```

## 8. Testing Strategy

### 8.1 Unit Tests (tests/spsc-queue/)

#### Correctness Tests
- **Basic Operations**: Push/pop across wrap boundary
- **Data Integrity**: Verify data consistency in both mappings
- **Wrap Patterns**: Test all wrap scenarios
- **Concurrent Access**: Reader/writer race conditions
- **Memory Barriers**: Verify ordering guarantees

#### Platform Tests  
- **Feature Detection**: Verify compile and runtime detection
- **Fallback Behavior**: Test graceful degradation
- **Error Paths**: Resource exhaustion, permission errors
- **Fork Safety**: Behavior across fork() calls

#### Edge Cases
- **Buffer Sizes**: Non-page-aligned, very small/large
- **Alignment**: Different T types and alignments
- **Overflow**: Index wraparound at size_t boundary
- **Resource Limits**: File descriptor exhaustion

### 8.2 Performance Benchmarks

```cpp
class DoublyMappedBenchmark : public ::testing::Test {
    void BenchmarkScenario(BufferStrategy strategy) {
        // Measure:
        // - Throughput (MB/s)
        // - System calls per MB
        // - CPU usage
        // - Cache misses
        // - Context switches
    }
};
```

### 8.3 Integration Tests

- **File Recording**: Large file recording with small buffers
- **Network Streaming**: UDP sink performance
- **Signal Processing**: Real-time audio/RF workflows
- **Stress Testing**: Long-running high-throughput scenarios

### 8.4 Compatibility Tests

- **Compiler Matrix**: GCC 7+, Clang 6+, MSVC 2017+
- **Platform Matrix**: Linux variants, macOS versions, BSDs
- **Container Environments**: Docker, Kubernetes pods
- **VM Environments**: Various hypervisors

## 9. Performance Expectations

### Throughput Improvements
- **Sequential writes**: 10-30% improvement
- **Small buffer sizes**: Up to 50% improvement  
- **High wrap frequency**: 2x fewer system calls

### Resource Usage
- **Virtual memory**: 2x address space (same physical memory)
- **File descriptors**: +1 per doubly mapped buffer
- **CPU overhead**: Negligible after initial setup

## 10. Implementation Phases

### Phase 1: Core Infrastructure (Week 1-2)
- [ ] Platform detection in `cler_platform.hpp`
- [ ] DoublyMappedBuffer implementation
- [ ] Basic unit tests
- [ ] Fallback mechanisms

### Phase 2: SPSC Integration (Week 2-3)
- [ ] SPSCQueueAdaptive implementation
- [ ] Buffer strategy selection logic
- [ ] Capability detection
- [ ] Performance benchmarks

### Phase 3: Channel Layer (Week 3-4)
- [ ] ChannelBase interface extensions
- [ ] Backward compatibility testing
- [ ] Documentation updates
- [ ] Example programs

### Phase 4: Block Integration (Week 4-5)
- [ ] File sink optimization
- [ ] Other applicable sinks (UDP, etc.)
- [ ] Real-world testing
- [ ] Performance validation

### Phase 5: Polish & Release (Week 5-6)
- [ ] Code review feedback
- [ ] Additional platform testing
- [ ] Documentation completion
- [ ] Migration guide

## 11. Risks and Mitigations

### Technical Risks

1. **mmap Failures**
   - Risk: Address space exhaustion, permission issues
   - Mitigation: Graceful fallback, clear error messages
   - Detection: Runtime capability testing

2. **Platform Incompatibility**
   - Risk: Subtle differences in mmap behavior
   - Mitigation: Comprehensive platform testing matrix
   - Detection: CI/CD on multiple platforms

3. **Memory Coherency**
   - Risk: Cache coherency issues on some architectures
   - Mitigation: Proper memory barriers, architecture testing
   - Detection: Stress tests on ARM, POWER

4. **Debugging Complexity**
   - Risk: Confusing debugger behavior with aliased memory
   - Mitigation: Debug mode flags, clear documentation
   - Detection: Developer feedback

### Performance Risks

1. **TLB Pressure**
   - Risk: Many buffers might exhaust TLB entries
   - Mitigation: Document limits, consider huge pages
   - Measurement: TLB miss counters

2. **Page Faults**
   - Risk: Initial access causes page faults
   - Mitigation: Optional prefaulting, MAP_POPULATE
   - Measurement: Page fault counters

## 12. Future Enhancements

### Near Term
- Windows support via `CreateFileMapping`/`MapViewOfFile`
- Huge page support for large buffers
- io_uring integration for zero-copy I/O

### Long Term  
- GPU memory mapping for direct DMA
- Persistent memory support
- Remote memory access (RDMA)

## 13. Success Criteria

### Functional
- ✓ Transparent API - existing code continues working
- ✓ Graceful fallback on all platforms
- ✓ No memory leaks or resource leaks
- ✓ Thread-safe operation

### Performance
- ✓ ≥10% throughput improvement for file I/O
- ✓ 50% reduction in fwrite calls for wrapped buffers
- ✓ No regression for standard buffers
- ✓ <1μs overhead for capability detection

### Quality
- ✓ 100% unit test coverage
- ✓ Platform CI/CD passing
- ✓ Documentation complete
- ✓ Examples provided

## 14. Open Questions

1. Should we provide huge page support from day 1?
2. Should the default strategy be Auto or Standard for compatibility?
3. Should we add metrics/counters for monitoring fallback frequency?
4. Should we support runtime strategy switching?
5. How should we handle Windows in the future?

## 15. References

- [Linux memfd_create](https://man7.org/linux/man-pages/man2/memfd_create.2.html)
- [POSIX shm_open](https://pubs.opengroup.org/onlinepubs/9699919799/functions/shm_open.html)
- [mmap best practices](https://www.kernel.org/doc/html/latest/admin-guide/mm/concepts.html)
- [Circular buffer techniques](https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/)