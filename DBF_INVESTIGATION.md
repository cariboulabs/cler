# Doubly-Mapped Buffer (DBF) Investigation Summary

## Executive Summary

The doubly-mapped buffer implementation in the SPSC queue has an **architectural mismatch**. While the virtual memory double mapping works correctly, the circular buffer's capacity doesn't align with the page-aligned mapped size, creating a gap of uninitialized memory.

## The Core Problem

The issue is **NOT** with the virtual memory implementation - that works perfectly. The problem is that the circular buffer's capacity_ doesn't match the actual mapped size.

### What Should Happen (Theory)

With a doubly-mapped buffer of size N:
- Physical memory: N bytes allocated once
- Virtual memory: Same N bytes mapped twice consecutively
- Result: Can read/write N contiguous elements from ANY position without wraparound

Example with N=10:
```
Virtual addresses: [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19]
Physical mapping:  [0,1,2,3,4,5,6,7,8,9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
                   ^----- First mapping ----^^----- Second mapping -----^
```

### What Actually Happens (Reality)

1. **Capacity Mismatch**: 
   - User requests capacity 16384
   - Internal capacity_ = 16385 (adds +1 to prevent livelock)
   - Page-aligned allocation = 17408 elements (69632 bytes)
   - Double mapping starts at element 17408 (not at 16385!)
   - Result: Elements 16385-17407 contain uninitialized garbage

2. **Index Wraparound Issue**:
   - Circular buffer uses indices 0 to 16384 (capacity_ - 1)
   - When readIndex=16000 and available=1000, read_dbf() says "read 1000 elements"
   - But elements 16385-16999 are uninitialized!
   - The circular buffer never writes to indices >= capacity_

3. **The Critical Issue**:
   ```
   capacity_ = 16385 (requested 16384 + 1)
   Actual mapped size = 17408 (page-aligned)
   Second mapping starts at element 17408
   
   But the circular buffer logic expects:
   - Valid indices: 0 to 16384
   - Wraparound: index 16385 should map to index 0
   
   Result: Indices 16385-17407 contain uninitialized garbage!
   ```

## Problems Encountered

### 1. Initial Misunderstanding
- **Wrong assumption**: The "fix" that limited reads to `capacity - readIndex` was actually correct for the current implementation
- **Reality**: Without this limit, we read garbage data past the capacity boundary

### 2. Buffer Size Confusion
- **Issue**: Multiple "sizes" in play:
  - User-requested capacity (e.g., 16384)  
  - Internal capacity_ (16385 due to +1)
  - Page-aligned mapped size (17408)
- **Problem**: Double mapping works for mapped size, but queue logic uses capacity_

### 3. Test Failures Revealed the Truth
- Reading zeros after certain positions → Not a mapping failure, but uninitialized memory
- Pattern jumps (e.g., expecting 32770 but getting 16385) → Modulo arithmetic mismatch

### 4. Attempted Fixes That Failed
- Returning `min(available, capacity)` → Still wrong, allows reading past valid data
- Returning `min(available, mapped_size)` → Still wrong, queue doesn't use those indices
- Returning just `available` → Causes reading garbage past capacity_

## The Real Solution

To make DBF work **as intended** with **no compromises**, we have two approaches:

### Approach 1: Expand Logical Capacity (Original Proposal)
Align the circular buffer capacity with the page-aligned size by expanding capacity_ to match the physical allocation.

### Approach 2: Separate Logical and Physical Capacities (Recommended)

Keep logical capacity (`capacity_`) separate from physical capacity (`capacity_dbf_`) to maintain platform compatibility.

#### Key Design Principles

1. **Platform Compatibility**: Non-DBF platforms see no change in behavior
2. **Backward Compatibility**: Existing code continues to work unchanged
3. **Clear Separation**: Logical operations use `capacity_`, physical DBF operations use `capacity_dbf_`
4. **Safe Initialization**: All memory up to `capacity_dbf_` is properly initialized

#### Implementation Details

```cpp
// In AdaptiveHeapBuffer:
struct AdaptiveHeapBuffer {
  const std::size_t capacity_;      // Logical capacity (user request + 1)
  std::size_t capacity_dbf_;        // Physical capacity for DBF (page-aligned)
  // ... rest of members ...
  
  explicit AdaptiveHeapBuffer(const std::size_t capacity,
                           const Allocator &allocator = Allocator())
    : capacity_(capacity + 1), 
      capacity_dbf_(capacity + 1),  // Default to same as capacity_
      buffer_(nullptr), 
      allocator_(allocator) {
      
    // ... validation code ...
    
    if (cler::platform::supports_doubly_mapped_buffers() && 
        buffer_bytes >= DOUBLY_MAPPED_MIN_SIZE) {
        
        // Calculate page-aligned element count
        const std::size_t aligned_elements = aligned_bytes / sizeof(T);
        
        if (vmem_allocation_.create(aligned_bytes)) {
            // Set capacity_dbf_ to the aligned size
            capacity_dbf_ = aligned_elements;
            is_doubly_mapped_ = true;
            
            // Initialize ALL elements up to capacity_dbf_
            // This ensures no garbage in the extended region
        }
    }
    // For non-DBF platforms, capacity_dbf_ == capacity_
}
```

#### Why This Approach Works

1. **Compatibility**: Non-DBF platforms have `capacity_dbf_ == capacity_`, no behavior change
2. **Safety**: All memory from 0 to `capacity_dbf_-1` is initialized
3. **True DBF**: The gap between `capacity_` and `capacity_dbf_` is properly initialized
4. **Zero-Copy**: Can read contiguously up to `capacity_dbf_ - readIndex`

## Implementation Plan

### Phase 1: Core Infrastructure
1. **Update AdaptiveHeapBuffer**
   - Add `capacity_dbf_` member variable
   - Initialize `capacity_dbf_ = capacity_` by default
   - For DBF allocations, set `capacity_dbf_` to page-aligned size
   - Initialize ALL elements up to `capacity_dbf_`
   - Update destructor to handle `capacity_dbf_` range

2. **Update SPSCQueue**
   - Add `capacity_dbf()` method to expose DBF capacity
   - Keep `capacity()` method unchanged (returns `capacity_ - 1`)
   - All existing operations continue using `capacity_`

### Phase 2: DBF-Specific Methods
3. **Update read_dbf()**
   ```cpp
   // Maximum contiguous read is limited by physical capacity
   std::size_t max_contiguous = capacity_dbf - readIndex;
   std::size_t contiguous_readable = std::min(available, max_contiguous);
   return {ptr, contiguous_readable};
   ```

4. **Update write_dbf()**
   ```cpp
   // Maximum contiguous write is limited by physical capacity
   std::size_t max_contiguous = capacity_dbf - writeIndex;
   std::size_t contiguous_writable = std::min(space, max_contiguous);
   return {ptr, contiguous_writable};
   ```

5. **Keep Standard commit_read/commit_write**
   - They already wrap at `capacity_` boundary
   - No special DBF versions needed

### Phase 3: Verification and Testing
6. **Update verify_double_mapping()**
   - Test mapping at `capacity_dbf_` boundary (not `capacity_`)
   - Verify `buffer_[0]` mirrors at `buffer_[capacity_dbf_]`

7. **Create Comprehensive Tests**
   - Test reading across `capacity_` boundary with DBF
   - Verify all data in extended region is initialized
   - Test platforms without DBF support
   - Performance benchmarks

### Phase 4: Documentation
8. **Update Documentation**
   - Explain the dual-capacity design
   - Document DBF behavior and limitations
   - Provide usage examples

### Implementation Order
1. AdaptiveHeapBuffer changes (capacity_dbf_, initialization)
2. SPSCQueue capacity_dbf() method
3. Update read_dbf() and write_dbf()
4. Update verify_double_mapping()
5. Create/update tests
6. Documentation

### Key Invariants to Maintain
- `capacity_dbf_ >= capacity_` (always)
- For non-DBF: `capacity_dbf_ == capacity_`
- For DBF: `capacity_dbf_` is page-aligned
- All indices [0, capacity_dbf_) are valid and initialized
- Circular buffer logic uses `capacity_` for wraparound
- DBF methods can access up to `capacity_dbf_`

### Expected Outcomes
- True zero-copy reads across buffer wraparound on DBF platforms
- No behavior change on non-DBF platforms
- All memory accesses are safe and defined
- Performance improvement for DBF-enabled systems

## Things to Test

1. **Verify Double Mapping Actually Works**:
   ```cpp
   buffer_[0] = 42;
   assert(buffer_[mapped_size] == 42);  // Should pass if truly doubly-mapped
   ```

2. **Test Wraparound Scenarios**:
   - Write data near end of buffer
   - Verify reading with `read_dbf()` returns correct data across boundary

3. **Stress Test with Different Sizes**:
   - Powers of 2
   - Prime numbers  
   - Sizes that align perfectly with pages
   - Sizes that don't align with pages

4. **Performance Impact**:
   - Measure overhead of the wraparound handling
   - Compare with non-DBF implementation

## Things to Investigate

1. **Why the +1 for Capacity?**
   - Document clearly why preventing livelock requires +1
   - Can this be handled differently with DBF?

2. **Page Alignment Requirements**:
   - Current code aligns to page size, but should it align to 2^n for easier math?
   - What's the minimum alignment for different platforms?

3. **Alternative Implementations**:
   - Look at other DBF implementations (e.g., in audio processing libraries)
   - Consider if the abstraction is worth the complexity

4. **Memory Overhead**:
   - Currently allocating extra due to page alignment
   - Could we be smarter about allocation sizes?

## Critical Next Steps

1. **DECIDE ON THE APPROACH**: We must choose between:
   - Living with the current limitation (document it clearly)
   - Implementing proper wraparound handling
   - Redesigning the API

2. **ADD COMPREHENSIVE TESTS**: Current tests don't properly verify DBF behavior:
   - Test reading exactly at wraparound boundary
   - Test writing exactly at wraparound boundary  
   - Test with capacity that's not power of 2

3. **DOCUMENT THE BEHAVIOR**: Whatever approach we take, document:
   - Exactly what guarantees `read_dbf()` provides
   - When it falls back to regular behavior
   - Performance implications

## Key Insight

The current implementation treats doubly-mapped buffers as an optimization detail, but they actually require a fundamental change in how the circular buffer works. You can't just "bolt on" DBF to an existing circular buffer implementation - it needs to be designed in from the ground up.

**The most important realization**: Just because memory is doubly-mapped doesn't mean the circular buffer logic knows how to use it correctly!