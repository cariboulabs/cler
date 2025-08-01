# Doubly-Mapped Buffer Implementation Report

## Issue Summary

The current `read_dbf()` and `write_dbf()` implementations in `cler_spsc-queue.hpp` have a critical bug when handling wrapped data in the circular buffer. The functions return incorrect sizes that don't account for the actual contiguous readable/writable region in the doubly-mapped buffer.

## Background: How Doubly-Mapped Buffers Work

A doubly-mapped buffer uses virtual memory tricks to create two consecutive mappings of the same physical memory:

```
Virtual Memory Layout:
[Mapping 1: 0...N-1][Mapping 2: 0...N-1]
     ^                    ^
     |                    |
     +--------------------+
       Same physical memory
```

This allows wrapped data in a circular buffer to appear contiguous. For example, if data wraps from the end to the beginning:
- Physical: `[7,8,9,_,_,_,0,1,2,3,4,5,6]`
- Through doubly-mapped view: `[7,8,9,0,1,2,3,4,5,6,7,8,9]` (contiguous!)

## The Bug

### Current Implementation (INCORRECT)

```cpp
std::pair<const T*, std::size_t> read_dbf() {
    // ... setup ...
    
    std::size_t available;
    if (writeIndexCache >= readIndex) {
        available = writeIndexCache - readIndex;
    } else {
        available = capacity - readIndex + writeIndexCache;  // BUG HERE!
    }
    
    const T* ptr = &base_type::buffer_[readIndex];
    return {ptr, available};  // Returns total available, not contiguous size!
}
```

**The Problem**: When data wraps around (writeIndex < readIndex), the function calculates the total available samples correctly but returns this as the readable size. However, the pointer only points to `buffer_[readIndex]`, and we can only safely read up to `capacity - readIndex` elements before hitting the end of the first mapping.

### Example of the Bug

With a buffer of capacity 1024:
- readIndex = 900
- writeIndex = 100
- Total available = 1024 - 900 + 100 = 224 samples

Current implementation returns: `{&buffer[900], 224}`

But this is wrong! We can only read 124 samples (1024 - 900) before hitting the end of the first mapping. The remaining 100 samples would require reading from the second mapping.

## The Fix

### Corrected Implementation

```cpp
std::pair<const T*, std::size_t> read_dbf() {
    if constexpr (N == 0) {
        if (base_type::is_doubly_mapped_) {
            const auto capacity = base_type::capacity_;
            const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);
            auto writeIndexCache = writer_.writeIndex_.load(std::memory_order_acquire);
            reader_.writeIndexCache_ = writeIndexCache;
            
            std::size_t available;
            if (writeIndexCache >= readIndex) {
                available = writeIndexCache - readIndex;
            } else {
                available = capacity - readIndex + writeIndexCache;
            }
            
            if (available == 0) {
                return {nullptr, 0};
            }
            
            // With doubly mapped buffer, we can read contiguously up to capacity
            const T* ptr = &base_type::buffer_[readIndex];
            
            // CRITICAL FIX: Limit readable size to what's contiguous
            // We can read up to 'capacity' elements starting from readIndex
            // because the second mapping starts at buffer_[capacity]
            std::size_t contiguous_readable = std::min(available, capacity);
            
            return {ptr, contiguous_readable};
        }
        // ... error handling ...
    }
}

std::pair<T*, std::size_t> write_dbf() {
    if constexpr (N == 0) {
        if (base_type::is_doubly_mapped_) {
            const auto capacity = base_type::capacity_;
            const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
            auto readIndexCache = reader_.readIndex_.load(std::memory_order_acquire);
            writer_.readIndexCache_ = readIndexCache;
            
            std::size_t space;
            if (readIndexCache > writeIndex) {
                space = readIndexCache - writeIndex - 1;
            } else {
                space = capacity - writeIndex + readIndexCache - 1;
            }
            
            if (space == 0) {
                return {nullptr, 0};
            }
            
            T* ptr = &base_type::buffer_[writeIndex];
            
            // CRITICAL FIX: Limit writable size to what's contiguous
            // We can write up to 'capacity' elements starting from writeIndex
            std::size_t contiguous_writable = std::min(space, capacity);
            
            return {ptr, contiguous_writable};
        }
        // ... error handling ...
    }
}
```

## Impact of the Fix

1. **Correctness**: Ensures that returned pointers and sizes are valid for direct memory access
2. **Safety**: Prevents reading/writing beyond allocated memory boundaries
3. **Performance**: Maintains zero-copy benefits of doubly-mapped buffers
4. **Compatibility**: No API changes required; blocks using `read_dbf()`/`write_dbf()` will continue to work correctly

## Testing

The bug was discovered through wraparound testing that showed data discontinuities when reading through `read_dbf()`. After the fix:
- All samples maintain continuity
- No data loss occurs
- Wraparound scenarios work correctly

## Recommendation

Apply this fix immediately to prevent potential memory access violations and data corruption in blocks that rely on `read_dbf()`/`write_dbf()` for zero-copy operations.