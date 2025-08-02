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

## The Root Cause: Misaligned Boundaries

**TL;DR** – The wrap boundary and the alias boundary MUST be the same address. Right now they aren't.

### Current Misalignment

| Concept | Value | What the queue uses for % | What the MMU aliases |
|---------|-------|---------------------------|---------------------|
| `capacity_` (logical modulo) | 16,385 | wraps here | plain memory |
| `capacity_dbf_` (page-aligned) | 17,408 | ignored by modulo | second mapping starts here |

Because `commit_write()`/`commit_read()` still use `% capacity_`, the queue eventually wraps to index 0 while the data just written lives at indices 16,385 through 17,407 – a region the reader never touches.

**This is why after the first full lap you see zeros/garbage.**

## The Real Solution: The GNU Radio/FutureSDR Approach

After investigation, the correct approach is to **conform to what GNU Radio and FutureSDR do**. These battle-tested SDR frameworks have solved this elegantly.

### Why GNU Radio/FutureSDR "Just Work"

Those codebases use **ONE single capacity value**:

```
logical_capacity == physical_capacity == mmap_size/elements
```

They first round the requested "N + 1" up to the next page, then use that rounded size everywhere:

```cpp
user asks for 16384
internal = align_up(16384 + 1, page) → 17408
queue modulo boundary = 17408
second mapping starts at 17408
```

**Result**: Modulo arithmetic and MMU alias are perfectly aligned. Writing past index 17407 really shows up at index 0.

### The Key Invariant

**Wrap boundary == Alias boundary** is the only invariant you really need.

## Two Ways to Fix Our Implementation

### Option 1: Single Capacity (Simplest - Recommended)

Replace the two-capacity scheme with only `capacity_`:

```cpp
const std::size_t capacity_requested = user_N + 1;      // +1 for livelock
capacity_ = align_up(capacity_requested, page_bytes) / sizeof(T);  // always page aligned
// Delete capacity_dbf_; all modulo and MMU logic now uses the same number
```

- `capacity()` (the public method) returns `capacity_ - 1`
- All operations use the same aligned capacity
- Matches GNU Radio/FutureSDR exactly

### Option 2: Keep Both, But Modulo With The Bigger One

If you really want to preserve the "logical" vs "physical" distinction:

```cpp
const std::size_t MODULO = is_doubly_mapped_ ? capacity_dbf_ : capacity_;
nextWriteIndex = (writeIndex + count) % MODULO;
```

- Do the same change everywhere: `nextReadIndex`, `space`, `available`, etc.
- The public `capacity()` must still report `MODULO - 1`
- More complex but preserves the dual-capacity concept

## Implementation Details (Option 1 - Single Capacity)

### Minimal Patch

```diff
-  const std::size_t buffer_bytes = capacity_ * sizeof(T);
+  const std::size_t buffer_bytes = align_up(capacity_ * sizeof(T), page_size);

-  capacity_dbf_ = aligned_elements;
-  is_doubly_mapped_ = true;
+  capacity_ = aligned_elements;  // use the aligned size *everywhere*
+  is_doubly_mapped_ = true;
```

And later:
```diff
-  std::size_t capacity() const noexcept { return base_type::capacity_ - 1; }
+  std::size_t capacity() const noexcept { return base_type::capacity_ - 1; }
-  std::size_t capacity_dbf() const noexcept { ... }  // delete this method
```

All `% capacity_` expressions now implicitly match the MMU alias.

### Quick Sanity Checks

#### 1. Alias Probe
```cpp
queue.buffer()[0] = 0xdeadbeef;
assert(queue.buffer()[queue.capacity_/*page aligned*/] == 0xdeadbeef);  // must pass
```

#### 2. Producer → Consumer Across Boundary
```cpp
writeIndex = queue.capacity() - 50;
auto [ptr, n] = queue.write_dbf();      // n >= 100 if space permits
memset(ptr, 0xa5, 100);
queue.commit_write(100);

std::uint8_t tmp[100];
queue.readN(tmp, 100);                  // must read back 0xa5 bytes
```

If either fails, the mapping itself is wrong; otherwise the bug is in modulo math.

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

## Current Status and Next Steps

### Test Failures - Now We Know Why!

Our tests are failing because of **misaligned boundaries**:

1. **The Problem**: 
   - Queue wraps at `capacity_` (16,385)
   - MMU aliases at `capacity_dbf_` (17,408)
   - Data written to indices 16,385-17,407 is never read!

2. **The Solution**: Make wrap boundary == alias boundary

### Immediate Action Items

1. **Choose Implementation Approach**:
   - Option 1 (Recommended): Single capacity, page-aligned
   - Option 2: Keep dual capacity but fix modulo operations

2. **Implement the Fix**:
   - Update capacity calculation in AdaptiveHeapBuffer
   - Ensure all modulo operations use the same boundary
   - Remove capacity_dbf() if using Option 1

3. **Add Sanity Checks**:
   - Alias probe test (buffer[0] == buffer[capacity])
   - Cross-boundary read/write test
   - These will quickly reveal if the fix works

4. **Update Tests**:
   - Remove references to capacity_dbf() if using Option 1
   - Verify all tests pass with aligned boundaries

## Key Insights

### The Fundamental Rule
**Wrap boundary == Alias boundary** - This is the ONLY invariant that matters.

### Why Our Implementation Failed
We had two different boundaries:
- Modulo wrapped at `capacity_` (16,385)
- MMU aliased at `capacity_dbf_` (17,408)
- Result: Data written to the gap was never read

### Why GNU Radio/FutureSDR Succeed
They use ONE capacity value that's page-aligned from the start. The modulo arithmetic and MMU aliasing speak the same "language."

### The Elegance
Once boundaries are aligned:
- No special commit functions needed
- Regular modulo arithmetic "just works"
- DBF becomes completely transparent
- Writing past the end automatically appears at the beginning

**The lesson**: Don't overthink it. Align the boundaries and let the existing circular buffer logic do its job. This is why GNU Radio and FutureSDR's approach has stood the test of time.