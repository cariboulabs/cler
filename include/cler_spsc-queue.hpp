// Copyright (c) 2024 Andrew Drogalis
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

#ifndef DRO_SPSC_QUEUE
#define DRO_SPSC_QUEUE

#include <array>       // for std::array
#include <atomic>      // for atomic, memory_order
#include <cstddef>     // for size_t
#include <limits>      // for numeric_limits
#include <stdexcept>   // for std::logic_error
#include <type_traits> // for std::is_default_constructible
#include <utility>     // for forward
#include <cstring>     // for std::memcpy
#include <memory>      // for std::allocator, placement new

// Cross-platform compatibility
#if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
#include <new>         // for std::hardware_destructive_interference_size
#endif

// Configuration for embedded systems
// Define DRO_SPSC_NO_EXCEPTIONS to disable exception throwing
// In this mode, functions return error codes instead of throwing
#ifndef DRO_SPSC_NO_EXCEPTIONS
#define DRO_SPSC_EXCEPTIONS_ENABLED 1
#else
#define DRO_SPSC_EXCEPTIONS_ENABLED 0
#endif

/*
 * ================================================================================================
 * CROSS-PLATFORM SPSC QUEUE - USAGE EXAMPLES
 * ================================================================================================
 * 
 * This SPSC queue implementation is designed for maximum cross-platform compatibility:
 * - Works on ARM/STM32/TI/Intel with optimal cache line alignment
 * - C++17 compatible (no C++20 concepts required)
 * - No std::vector dependency (embedded-friendly)
 * - Optional exception-free mode for embedded systems
 * - Support for custom allocators including static memory pools
 *
 * ================================================================================================
 * BASIC USAGE EXAMPLES:
 * ================================================================================================
 *
 * 1. STACK ALLOCATION (Embedded-friendly, zero heap usage):
 *    ----------------------------------------------------------
 *    dro::SPSCQueue<int, 1024> stack_queue;  // 1024 elements on stack
 *    stack_queue.push(42);
 *    int value;
 *    stack_queue.pop(value);
 *    
 *    → Creates: StackBuffer with std::array<int, 1024 + padding>
 *    → Memory: 100% stack allocated, zero heap usage
 *    → Platform: Cache-aligned for your CPU (32/64 byte detection)
 *    → Suitable: STM32, embedded systems with limited heap
 *
 * 2. DYNAMIC ALLOCATION (Default, still no std::vector):
 *    ----------------------------------------------------
 *    dro::SPSCQueue<int> heap_queue(2048);  // 2048 elements
 *    heap_queue.push(123);
 *    int value;
 *    heap_queue.pop(value);
 *    
 *    → Creates: DynamicBuffer using std::allocator
 *    → Memory: Heap allocated via allocator.allocate() + placement new
 *    → No vector: Direct memory control, no automatic element construction
 *    → Platform: Optimal cache alignment for current architecture
 *
 * 3. CUSTOM ALLOCATOR - Static Pool (No heap at all):
 *    -------------------------------------------------
 *    cler::StaticPoolAllocator<8192> pool;  // 8KB static pool
 *    dro::SPSCQueue<int, 0, decltype(pool)> pool_queue(512, pool);
 *    
 *    → Creates: DynamicBuffer using static memory pool
 *    → Memory: Pre-allocated static memory, zero heap calls
 *    → Suitable: Real-time systems, RTOS, bare-metal embedded
 *    → Deterministic: No dynamic allocation after initialization
 *
 * 4. CUSTOM ALLOCATOR - User Memory Region:
 *    ----------------------------------------
 *    static int dma_memory[1024];  // DMA-accessible memory region
 *    cler::RegionAllocator<int> region(dma_memory, 1024);
 *    dro::SPSCQueue<int, 0, decltype(region)> dma_queue(256, region);
 *    
 *    → Creates: DynamicBuffer using specific memory region
 *    → Memory: User-controlled location (could be DMA, fast SRAM, etc.)
 *    → Use case: Hardware that requires data in specific memory regions
 *
 * ================================================================================================
 * EXCEPTION-FREE MODE (For embedded systems):
 * ================================================================================================
 *
 * Compile with -DDRO_SPSC_NO_EXCEPTIONS to disable exceptions:
 *
 *    dro::SPSCQueue<int> queue(1024);
 *    if (!queue.is_valid()) {
 *        // Handle construction failure without exceptions
 *        auto error = queue.get_construction_error();
 *        // Handle error appropriately
 *    }
 *    
 *    → Benefits: No exception handling overhead
 *    → Suitable: Embedded systems with exceptions disabled
 *    → Error handling: Return codes instead of throwing
 *
 * ================================================================================================
 * PLATFORM-SPECIFIC OPTIMIZATIONS:
 * ================================================================================================
 *
 * The queue automatically detects your platform and optimizes cache alignment:
 *
 * → Intel x86/x64:           64-byte cache lines
 * → ARM Cortex-M (STM32/TI): 32-byte cache lines  
 * → ARM Cortex-A:            64-byte cache lines
 * → Generic ARM:             32-byte cache lines (conservative)
 * → Unknown platforms:       64-byte cache lines (safe default)
 *
 * This ensures optimal performance across all your target architectures
 * without any code changes or conditional compilation.
 *
 * ================================================================================================
 * THREADING AND PERFORMANCE:
 * ================================================================================================
 *
 * Single Producer, Single Consumer (SPSC) design:
 * → One thread pushes data (producer)
 * → One thread pops data (consumer)  
 * → Lock-free operations for maximum performance
 * → Memory ordering optimized for your platform
 *
 * Example multi-threaded usage:
 *
 *    dro::SPSCQueue<float, 4096> audio_queue;  // Stack allocated
 *    
 *    // Producer thread (audio callback):
 *    std::thread producer([&]() {
 *        while (running) {
 *            float sample = get_audio_sample();
 *            audio_queue.push(sample);  // Lock-free
 *        }
 *    });
 *    
 *    // Consumer thread (processing):
 *    std::thread consumer([&]() {
 *        while (running) {
 *            float sample;
 *            if (audio_queue.try_pop(sample)) {  // Non-blocking
 *                process_audio_sample(sample);
 *            }
 *        }
 *    });
 *
 * ================================================================================================
 * MIGRATION FROM std::vector-based implementations:
 * ================================================================================================
 *
 * This implementation replaces std::vector with direct allocator usage:
 *
 * OLD (vector-based):              NEW (embedded-friendly):
 * ├─ std::vector<T> buffer         ├─ T* buffer + manual construction
 * ├─ Automatic element init        ├─ Controlled element lifecycle  
 * ├─ Exception-heavy resize()      ├─ Predictable allocation behavior
 * ├─ Hidden reallocations          ├─ No hidden memory operations
 * ├─ STL dependency                ├─ Minimal standard library usage
 * └─ Desktop-focused               └─ Embedded + desktop compatible
 *
 * Benefits of the new approach:
 * → Deterministic memory usage
 * → Support for custom memory regions
 * → Reduced binary size (less STL usage)
 * → Better embedded toolchain compatibility
 * → Same performance, more control
 */

namespace dro {

#if !DRO_SPSC_EXCEPTIONS_ENABLED
// Error codes for exception-free mode
enum class SPSCError {
    SUCCESS = 0,
    INVALID_CAPACITY,
    CAPACITY_OVERFLOW,
    ALLOCATION_FAILED
};

// Error handling helper for embedded systems
template<typename T>
struct SPSCResult {
    T value;
    SPSCError error;
    
    explicit operator bool() const { return error == SPSCError::SUCCESS; }
    bool has_error() const { return error != SPSCError::SUCCESS; }
};
#endif

namespace details {

// Cross-platform cache line size detection
// Optimized for ARM/STM32/TI/Intel architectures
#if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
static constexpr std::size_t cacheLineSize = std::hardware_destructive_interference_size;
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
// Intel x86/x64: 64 bytes
static constexpr std::size_t cacheLineSize = 64;
#elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7EM__)
// ARM Cortex-M (STM32, TI Tiva): 32 bytes
static constexpr std::size_t cacheLineSize = 32;
#elif defined(__ARM_ARCH) && (__ARM_ARCH >= 8)
// ARM Cortex-A (64-bit): 64 bytes  
static constexpr std::size_t cacheLineSize = 64;
#elif defined(__ARM_ARCH) && (__ARM_ARCH == 7)
// ARM Cortex-A (32-bit): typically 64 bytes
static constexpr std::size_t cacheLineSize = 64;
#elif defined(__aarch64__)
// ARM64: 64 bytes
static constexpr std::size_t cacheLineSize = 64;
#elif defined(__arm__) || defined(_M_ARM)
// Generic ARM: conservative 32 bytes
static constexpr std::size_t cacheLineSize = 32;
#else
// Safe default for unknown platforms
static constexpr std::size_t cacheLineSize = 64;
#endif

static constexpr std::size_t MAX_BYTES_ON_STACK = 2'097'152; // 2 MBs

// C++17 compatible type traits (replacing C++20 concepts)

// SPSC_Type: ensures T is suitable for SPSC operations
template <typename T>
struct is_spsc_type {
    static constexpr bool value = 
        std::is_default_constructible<T>::value &&
        std::is_nothrow_destructible<T>::value &&
        (std::is_move_assignable<T>::value || std::is_copy_assignable<T>::value);
};

template <typename T>
using enable_if_spsc_type_t = std::enable_if_t<is_spsc_type<T>::value>;

// SPSC_NoThrow_Type: ensures T operations are noexcept
template <typename T, typename... Args>
struct is_spsc_nothrow_type {
    static constexpr bool value = 
        std::is_nothrow_constructible<T, Args&&...>::value &&
        ((std::is_nothrow_copy_assignable<T>::value && std::is_copy_assignable<T>::value) ||
         (std::is_nothrow_move_assignable<T>::value && std::is_move_assignable<T>::value));
};

template <typename T, typename... Args>
using enable_if_spsc_nothrow_type_t = std::enable_if_t<is_spsc_nothrow_type<T, Args...>::value>;

// MAX_STACK_SIZE: prevents stack overflow
template <typename T, std::size_t N>
struct is_valid_stack_size {
    static constexpr bool value = (N <= (MAX_BYTES_ON_STACK / sizeof(T)));
};

template <typename T, std::size_t N>
using enable_if_valid_stack_size_t = std::enable_if_t<is_valid_stack_size<T, N>::value>;

// Custom Dynamic Buffer (Vector-Free Implementation)
template <typename T, typename Allocator = std::allocator<T>, typename = enable_if_spsc_type_t<T>>
struct DynamicBuffer {
  const std::size_t capacity_;
  T* buffer_;
  const std::size_t total_size_;
  Allocator allocator_;

  static constexpr std::size_t padding = ((cacheLineSize - 1) / sizeof(T)) + 1;
  static constexpr std::size_t MAX_SIZE_T =
      std::numeric_limits<std::size_t>::max();

  // Helper to handle different allocator interfaces
  T* allocate_impl(Allocator& alloc, std::size_t n) {
    return allocate_dispatch(alloc, n, int{});
  }
  
private:
  // Priority-based overload resolution: try template allocate first
  template<typename Alloc>
  auto allocate_dispatch(Alloc& alloc, std::size_t n, int) 
    -> decltype(alloc.template allocate<T>(n)) {
    return alloc.template allocate<T>(n);
  }
  
  // Fallback for std::allocator and RegionAllocator (non-template allocate)
  template<typename Alloc>
  auto allocate_dispatch(Alloc& alloc, std::size_t n, long) 
    -> decltype(alloc.allocate(n)) {
    return alloc.allocate(n);
  }

public:

  explicit DynamicBuffer(const std::size_t capacity,
                         const Allocator &allocator = Allocator())
      // +1 prevents live lock e.g. reader and writer share 1 slot for size 1
      : capacity_(capacity + 1)
      , buffer_(nullptr)
      , total_size_(capacity_ + (2 * padding))
      , allocator_(allocator) {
#if DRO_SPSC_EXCEPTIONS_ENABLED
    if (capacity < 1) {
      throw std::logic_error("Capacity must be a positive number; Dynamic "
                             "allocations require capacity argument");
    }
    // (2 * padding) is for preventing cache contention between adjacent memory
    if (capacity_ > MAX_SIZE_T - (2 * padding)) {
      throw std::overflow_error(
          "Capacity with padding exceeds std::size_t. Reduce size of queue.");
    }
#else
    // Exception-free mode: validate but don't throw
    if (capacity < 1 || capacity_ > MAX_SIZE_T - (2 * padding)) {
      // Mark as invalid - caller must check
      const_cast<std::size_t&>(capacity_) = 0;
      return;
    }
#endif

    // Allocate raw memory using allocator
    buffer_ = allocate_impl(allocator_, total_size_);
    
    if (!buffer_) {
#if DRO_SPSC_EXCEPTIONS_ENABLED
      throw std::bad_alloc();
#else
      const_cast<std::size_t&>(capacity_) = 0;
      return;
#endif
    }

    // Default construct all elements
    for (std::size_t i = 0; i < total_size_; ++i) {
      new (buffer_ + i) T();
    }
  }

  ~DynamicBuffer() {
    if (buffer_) {
      // Destroy all elements
      for (std::size_t i = 0; i < total_size_; ++i) {
        buffer_[i].~T();
      }
      // Deallocate memory
      allocator_.deallocate(buffer_, total_size_);
    }
  }

  // Non-Copyable and Non-Movable
  DynamicBuffer(const DynamicBuffer &lhs) = delete;
  DynamicBuffer &operator=(const DynamicBuffer &lhs) = delete;
  DynamicBuffer(DynamicBuffer &&lhs) = delete;
  DynamicBuffer &operator=(DynamicBuffer &&lhs) = delete;
};

// Memory Allocated on the Stack
template <typename T, std::size_t N, typename Allocator = std::allocator<T>, 
          typename = enable_if_spsc_type_t<T>, typename = enable_if_valid_stack_size_t<T, N>>
struct StackBuffer {
  // +1 prevents live lock e.g. reader and writer share 1 slot for size 1
  static constexpr std::size_t capacity_{N + 1};
  static constexpr std::size_t padding = ((cacheLineSize - 1) / sizeof(T)) + 1;
  // (2 * padding) is for preventing cache contention between adjacent memory
  std::array<T, capacity_ + (2 * padding)> buffer_;

  explicit StackBuffer(const std::size_t capacity,
                       [[maybe_unused]] const Allocator &allocator = Allocator()) {
#if DRO_SPSC_EXCEPTIONS_ENABLED
    if (capacity) {
      throw std::invalid_argument(
          "Capacity in constructor is ignored for stack allocations");
    }
#else
    // Exception-free mode: silently ignore capacity parameter
    // Stack allocations have fixed compile-time size
    (void)capacity; // Suppress unused parameter warning
#endif
  }

  ~StackBuffer() = default;
  // Non-Copyable and Non-Movable
  StackBuffer(const StackBuffer &lhs) = delete;
  StackBuffer &operator=(const StackBuffer &lhs) = delete;
  StackBuffer(StackBuffer &&lhs) = delete;
  StackBuffer &operator=(StackBuffer &&lhs) = delete;
};

} // namespace details

template <typename T, std::size_t N = 0, typename Allocator = std::allocator<T>,
          typename = details::enable_if_spsc_type_t<T>, 
          typename = details::enable_if_valid_stack_size_t<T, N>>
class SPSCQueue
    : public std::conditional_t<N == 0, details::DynamicBuffer<T, Allocator>,
                                details::StackBuffer<T, N>> {
private:
  using base_type =
      std::conditional_t<N == 0, details::DynamicBuffer<T, Allocator>,
                         details::StackBuffer<T, N>>;
  static constexpr bool nothrow_v = details::is_spsc_nothrow_type<T>::value;

  struct alignas(details::cacheLineSize) WriterCacheLine {
    std::atomic<std::size_t> writeIndex_{0};
    std::size_t readIndexCache_{0};
    // Reduces cache contention on very small queues
    const size_t paddingCache_ = base_type::padding;
  } writer_;

  struct alignas(details::cacheLineSize) ReaderCacheLine {
    std::atomic<std::size_t> readIndex_{0};
    std::size_t writeIndexCache_{0};
    // Reduces cache contention on very small queues
    std::size_t capacityCache_{};
  } reader_;

public:
  explicit SPSCQueue(const std::size_t capacity = 0,
                     const Allocator &allocator = Allocator())
      : base_type(capacity, allocator) {
    reader_.capacityCache_ = base_type::capacity_;
  }

#if !DRO_SPSC_EXCEPTIONS_ENABLED
  // Exception-free mode: check if construction was successful
  bool is_valid() const noexcept {
    return base_type::capacity_ > 0;
  }
  
  SPSCError get_construction_error() const noexcept {
    return is_valid() ? SPSCError::SUCCESS : SPSCError::INVALID_CAPACITY;
  }
#endif

  ~SPSCQueue() = default;
  // Non-Copyable and Non-Movable
  SPSCQueue(const SPSCQueue &lhs) = delete;
  SPSCQueue &operator=(const SPSCQueue &lhs) = delete;
  SPSCQueue(SPSCQueue &&lhs) = delete;
  SPSCQueue &operator=(SPSCQueue &&lhs) = delete;

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<T, Args&&...>::value>>
  void
  emplace(Args &&...args) noexcept(details::is_spsc_nothrow_type<T, Args&&...>::value) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    // Loop while waiting for reader to catch up
    while (nextWriteIndex == writer_.readIndexCache_) {
      writer_.readIndexCache_ =
          reader_.readIndex_.load(std::memory_order_acquire);
    }
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<T, Args&&...>::value>>
  void force_emplace(Args &&...args) noexcept(
      details::is_spsc_nothrow_type<T, Args&&...>::value) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<T, Args&&...>::value>>
  [[nodiscard]] bool try_emplace(Args &&...args) noexcept(
      details::is_spsc_nothrow_type<T, Args&&...>::value) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    // Check reader cache and if actually equal then fail to write
    if (nextWriteIndex == writer_.readIndexCache_) {
      writer_.readIndexCache_ =
          reader_.readIndex_.load(std::memory_order_acquire);
      if (nextWriteIndex == writer_.readIndexCache_) {
        return false;
      }
    }
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
    return true;
  }

  void push(const T &val) noexcept(nothrow_v) { emplace(val); }

  template <typename P, typename = std::enable_if_t<std::is_constructible<T, P&&>::value>>
  void push(P &&val) noexcept(details::is_spsc_nothrow_type<T, P&&>::value) {
    emplace(std::forward<P>(val));
  }

  void force_push(const T &val) noexcept(nothrow_v) { force_emplace(val); }

  template <typename P, typename = std::enable_if_t<std::is_constructible<T, P&&>::value>>
  void force_push(P &&val) noexcept(details::is_spsc_nothrow_type<T, P&&>::value) {
    force_emplace(std::forward<P>(val));
  }

  [[nodiscard]] bool try_push(const T &val) noexcept(nothrow_v) {
    return try_emplace(val);
  }

  template <typename P, typename = std::enable_if_t<std::is_constructible<T, P&&>::value>>
  [[nodiscard]] bool
  try_push(P &&val) noexcept(details::is_spsc_nothrow_type<T, P&&>::value) {
    return try_emplace(std::forward<P>(val));
  }

  void pop(T &val) noexcept(nothrow_v) {
    const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);
    // Loop while waiting for writer to enqueue
    while (readIndex == reader_.writeIndexCache_) {
      reader_.writeIndexCache_ =
          writer_.writeIndex_.load(std::memory_order_acquire);
    }
    val = read_value(readIndex);
    const auto nextReadIndex =
        (readIndex == reader_.capacityCache_ - 1) ? 0 : readIndex + 1;
    reader_.readIndex_.store(nextReadIndex, std::memory_order_release);
  }

  [[nodiscard]] bool try_pop(T &val) noexcept(nothrow_v) {
    const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);
    // Check writer cache and if actually equal then fail to read
    if (readIndex == reader_.writeIndexCache_) {
      reader_.writeIndexCache_ =
          writer_.writeIndex_.load(std::memory_order_acquire);
      if (readIndex == reader_.writeIndexCache_) {
        return false;
      }
    }
    val = read_value(readIndex);
    const auto nextReadIndex =
        (readIndex == reader_.capacityCache_ - 1) ? 0 : readIndex + 1;
    reader_.readIndex_.store(nextReadIndex, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_acquire);
    const auto readIndex = reader_.readIndex_.load(std::memory_order_acquire);
    // This method prevents conversion to std::ptrdiff_t (a signed type)
    if (writeIndex >= readIndex) {
      return writeIndex - readIndex;
    }
    return (base_type::capacity_ - readIndex) + writeIndex;
  }

  [[nodiscard]] bool empty() const noexcept {
    return writer_.writeIndex_.load(std::memory_order_acquire) ==
           reader_.readIndex_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return base_type::capacity_ - 1;
  }

  [[nodiscard]] std::size_t space() const noexcept {
    return capacity() - size();
  }

  std::size_t writeN(const T* src, std::size_t count) noexcept(nothrow_v) {
    const auto capacity = base_type::capacity_;
    const auto padding  = writer_.paddingCache_;
    auto writeIndex     = writer_.writeIndex_.load(std::memory_order_relaxed);

    const auto nextWriteIndex = (writeIndex + count) % capacity;
    auto readIndexCache = reader_.readIndex_.load(std::memory_order_acquire);
    writer_.readIndexCache_ = readIndexCache;

    std::size_t space;
    if (readIndexCache > writeIndex) {
      space = readIndexCache - writeIndex - 1;
    } else {
      space = capacity - writeIndex + readIndexCache - 1;
    }

    const std::size_t toWrite = std::min(count, space);
    if (toWrite == 0) return 0;

    const std::size_t firstChunk = std::min(toWrite, capacity - writeIndex);

    std::memcpy(&base_type::buffer_[writeIndex + padding], src, firstChunk * sizeof(T));

    if (firstChunk < toWrite) {
      std::memcpy(&base_type::buffer_[padding], src + firstChunk, (toWrite - firstChunk) * sizeof(T));
    }

    writer_.writeIndex_.store((writeIndex + toWrite) % capacity, std::memory_order_release);
    return toWrite;
  }

  std::size_t force_writeN(const T* src, std::size_t count) noexcept(nothrow_v) {
    const auto capacity = base_type::capacity_;
    const auto padding  = writer_.paddingCache_;
    auto writeIndex     = writer_.writeIndex_.load(std::memory_order_relaxed);

    // Calculate next write index no matter what
    const auto nextWriteIndex = (writeIndex + count) % capacity;

    // If we're overwriting unread data, advance reader index
    auto readIndex = reader_.readIndex_.load(std::memory_order_acquire);

    std::size_t used_space;
    if (readIndex > writeIndex) {
      used_space = writeIndex + (capacity - readIndex);
    } else {
      used_space = writeIndex - readIndex;
    }

    if (count > capacity - 1) {
      // Defensive: never force write more than capacity-1
      count = capacity - 1;
    }

    if (count > (capacity - 1 - used_space)) {
      // Not enough space, so move reader forward to make room
      std::size_t advance = count - (capacity - 1 - used_space);
      auto newReadIndex = (readIndex + advance) % capacity;
      reader_.readIndex_.store(newReadIndex, std::memory_order_release);
    }

    const std::size_t firstChunk = std::min(count, capacity - writeIndex);

    std::memcpy(&base_type::buffer_[writeIndex + padding], src, firstChunk * sizeof(T));

    if (firstChunk < count) {
      std::memcpy(&base_type::buffer_[padding], src + firstChunk, (count - firstChunk) * sizeof(T));
    }

    writer_.writeIndex_.store((writeIndex + count) % capacity, std::memory_order_release);

    return count;
  }

  std::size_t readN(T* dst, std::size_t count) noexcept(nothrow_v) {
    const auto capacity = base_type::capacity_;
    const auto padding  = base_type::padding;

    auto readIndex  = reader_.readIndex_.load(std::memory_order_relaxed);
    auto writeIndex = writer_.writeIndex_.load(std::memory_order_acquire);
    reader_.writeIndexCache_ = writeIndex;

    std::size_t available;
    if (writeIndex >= readIndex) {
      available = writeIndex - readIndex;
    } else {
      available = capacity - readIndex + writeIndex;
    }

    const std::size_t toRead = std::min(count, available);
    if (toRead == 0) return 0;

    const std::size_t firstChunk = std::min(toRead, capacity - readIndex);

    std::memcpy(dst, &base_type::buffer_[readIndex + padding], firstChunk * sizeof(T));

    if (firstChunk < toRead) {
      std::memcpy(dst + firstChunk, &base_type::buffer_[padding], (toRead - firstChunk) * sizeof(T));
    }

    const auto nextReadIndex = (readIndex + toRead) % capacity;
    reader_.readIndex_.store(nextReadIndex, std::memory_order_release);

    return toRead;
  }

std::size_t peek_write(T*& ptr1, std::size_t& size1, T*& ptr2, std::size_t& size2) noexcept {
  const auto capacity = base_type::capacity_;
  const auto padding  = writer_.paddingCache_;
  const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);

  auto readIndexCache = writer_.readIndexCache_;
  readIndexCache = reader_.readIndex_.load(std::memory_order_acquire);
  writer_.readIndexCache_ = readIndexCache;

  std::size_t space;
  if (readIndexCache > writeIndex) {
    space = readIndexCache - writeIndex - 1;
  } else {
    space = capacity - writeIndex + readIndexCache - 1;
  }

  if (space == 0) {
    ptr1 = nullptr;
    ptr2 = nullptr;
    size1 = 0;
    size2 = 0;
    return 0;
  }

  // First chunk: contiguous to end
  std::size_t first_chunk = (readIndexCache > writeIndex)
      ? space  // contiguous, no wrap
      : capacity - writeIndex;

  ptr1 = &base_type::buffer_[writeIndex + padding];
  size1 = first_chunk;

  if (readIndexCache <= writeIndex) {
    // Wrapped: second chunk exists
    ptr2 = &base_type::buffer_[padding];
    size2 = readIndexCache - 1;
  } else {
    ptr2 = nullptr;
    size2 = 0;
  }

  return space;
}

void commit_write(std::size_t count) noexcept {
  const auto capacity = base_type::capacity_;
  const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
  const auto nextWriteIndex = (writeIndex + count) % capacity;
  writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
}

std::size_t peek_read(const T*& ptr1, std::size_t& size1,  const T*& ptr2, std::size_t& size2) noexcept {
  const auto capacity = base_type::capacity_;
  const auto padding  = base_type::padding;

  const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);

  auto writeIndexCache = reader_.writeIndexCache_;
  writeIndexCache = writer_.writeIndex_.load(std::memory_order_acquire);
  reader_.writeIndexCache_ = writeIndexCache;

  std::size_t available;
  if (writeIndexCache >= readIndex) {
    available = writeIndexCache - readIndex;
  } else {
    available = capacity - readIndex + writeIndexCache;
  }

  if (available == 0) {
    ptr1 = nullptr;
    ptr2 = nullptr;
    size1 = 0;
    size2 = 0;
    return 0;
  }

  // First chunk: contiguous
  std::size_t first_chunk = (writeIndexCache >= readIndex)
      ? available   // no wrap
      : capacity - readIndex;

  ptr1 = &base_type::buffer_[readIndex + padding];
  size1 = first_chunk;

  if (writeIndexCache < readIndex) {
    // Wrapped, so second chunk exists
    ptr2 = &base_type::buffer_[padding];
    size2 = writeIndexCache;
  } else {
    ptr2 = nullptr;
    size2 = 0;
  }

  return available;
}

void commit_read(std::size_t count) noexcept {
  const auto capacity = base_type::capacity_;
  const auto readIndex = reader_.readIndex_.load(std::memory_order_relaxed);
  const auto nextReadIndex = (readIndex + count) % capacity;
  reader_.readIndex_.store(nextReadIndex, std::memory_order_release);
}



private:
  // Note: The "+ padding" is a constant offset used to prevent false sharing
  // with memory in front of the SPSC allocations
  template<typename U = T>
  typename std::enable_if_t<std::is_copy_assignable<U>::value && (!std::is_move_assignable<U>::value), T&>
  read_value(const std::size_t &readIndex) noexcept(nothrow_v)
  {
    return base_type::buffer_[readIndex + base_type::padding];
  }

  template<typename U = T>
  typename std::enable_if_t<std::is_move_assignable<U>::value, T&&>
  read_value(const std::size_t &readIndex) noexcept(nothrow_v)
  {
    return std::move(base_type::buffer_[readIndex + base_type::padding]);
  }

  template<typename U = T>
  typename std::enable_if_t<std::is_copy_assignable<U>::value && (!std::is_move_assignable<U>::value), void>
  write_value(const std::size_t &writeIndex, T &val) noexcept(nothrow_v)
  {
    base_type::buffer_[writeIndex + writer_.paddingCache_] = val;
  }

  template<typename U = T>
  typename std::enable_if_t<std::is_move_assignable<U>::value, void>
  write_value(const std::size_t &writeIndex, T &&val) noexcept(nothrow_v)
  {
    base_type::buffer_[writeIndex + writer_.paddingCache_] = std::move(val);
  }

  template <typename... Args>
  typename std::enable_if_t<(std::is_constructible<T, Args&&...>::value &&
             std::is_copy_assignable<T>::value && (!std::is_move_assignable<T>::value)), void>
  write_value(const std::size_t &writeIndex, Args &&...args) noexcept(
      details::is_spsc_nothrow_type<T, Args&&...>::value) {
    T copyOnly{std::forward<Args>(args)...};
    base_type::buffer_[writeIndex + writer_.paddingCache_] = copyOnly;
  }

  template <typename... Args>
  typename std::enable_if_t<(std::is_constructible<T, Args&&...>::value &&
             std::is_move_assignable<T>::value), void>
  write_value(const std::size_t &writeIndex, Args &&...args) noexcept(
      details::is_spsc_nothrow_type<T, Args&&...>::value) {
    base_type::buffer_[writeIndex + writer_.paddingCache_] =
        T(std::forward<Args>(args)...);
  }
};

} // namespace dro
#endif
