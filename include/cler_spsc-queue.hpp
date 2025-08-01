// Copyright (c) 2024 Andrew Drogalis
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
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
#include <new>         // for std::hardware_destructive_interference_size
#include <stdexcept>   // for std::logic_error
#include <type_traits> // for std::is_default_constructible
#include <utility>     // for forward
#include <cstring>     // for std::memcpy
#include "cler_platform.hpp"

// Include platform-specific virtual memory support
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    #include "virtual_memory/cler_vmem_posix.hpp"
#elif defined(_WIN32)
    #include "virtual_memory/cler_vmem_win.hpp"
#else
    #include "virtual_memory/cler_vmem_none.hpp"
#endif

namespace dro {

namespace details {

// Use platform-aware cache line size from cler_platform.hpp
static constexpr std::size_t cacheLineSize = cler::platform::cache_line_size;

static constexpr std::size_t MAX_BYTES_ON_STACK = 2'097'152; // 2 MBs

 static constexpr std::size_t DOUBLY_MAPPED_MIN_SIZE = 4096; // 4 Kbs

// C++17 version using type traits
template <typename T>
using SPSC_Type = std::enable_if_t<
    std::is_default_constructible<T>::value &&
    std::is_nothrow_destructible<T>::value &&
    (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>)
>;

// C++17 version using type traits
template <typename T, typename... Args>
using SPSC_NoThrow_Type = std::enable_if_t<
    std::is_nothrow_constructible_v<T, Args &&...> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))
>;

// Prevents Stack Overflow - C++17 version
template <typename T, std::size_t N>
using MAX_STACK_SIZE = std::enable_if_t<(N <= (MAX_BYTES_ON_STACK / sizeof(T)))>;

// Enhanced heap buffer that tries doubly mapped first, falls back to standard
template <typename T, typename Allocator = std::allocator<T>,
          typename = SPSC_Type<T>>
struct AdaptiveHeapBuffer {
  const std::size_t capacity_;
  T* buffer_;
  [[no_unique_address]] Allocator allocator_;
  bool is_doubly_mapped_ = false;

  static constexpr std::size_t padding = ((cacheLineSize - 1) / sizeof(T)) + 1;
  static constexpr std::size_t MAX_SIZE_T = std::numeric_limits<std::size_t>::max();

private:
  cler::vmem::DoublyMappedAllocation vmem_allocation_;
  T* raw_allocation_ = nullptr;  // For standard allocation cleanup
  
public:
  explicit AdaptiveHeapBuffer(const std::size_t capacity,
                             const Allocator &allocator = Allocator())
      // +1 prevents live lock e.g. reader and writer share 1 slot for size 1
      : capacity_(capacity + 1), buffer_(nullptr), allocator_(allocator) {
    if (capacity < 1) {
      throw std::logic_error("Capacity must be a positive number; Heap "
                             "allocations require capacity argument");
    }
    // (2 * padding) is for preventing cache contention between adjacent memory
    if (capacity_ > MAX_SIZE_T - (2 * padding)) {
      throw std::overflow_error(
          "Capacity with padding exceeds std::size_t. Reduce size of queue.");
    }
    
    const std::size_t buffer_bytes = capacity_ * sizeof(T);
    
    // Try doubly mapped allocation for buffers ≥32KB (typical SDR buffer size)
    if (cler::platform::supports_doubly_mapped_buffers() && 
        buffer_bytes >= DOUBLY_MAPPED_MIN_SIZE) {
      
      // For doubly mapped buffers, we don't need cache-line padding
      // Just allocate exactly what we need
      if (vmem_allocation_.create(buffer_bytes)) {
        buffer_ = static_cast<T*>(vmem_allocation_.data());
        if (buffer_) {
          is_doubly_mapped_ = true;
          
          // Initialize elements if needed
          if constexpr (!std::is_trivially_constructible_v<T>) {
            for (std::size_t i = 0; i < capacity_; ++i) {
              std::allocator_traits<Allocator>::construct(allocator_, buffer_ + i);
            }
          }
          return; // Success!
        }
      }
    }
    
    // Fallback to standard heap allocation WITH padding
    const std::size_t total_size = capacity_ + (2 * padding);
    raw_allocation_ = std::allocator_traits<Allocator>::allocate(allocator_, total_size);
    
    // Initialize all elements including padding
    if constexpr (!std::is_trivially_constructible_v<T>) {
      for (std::size_t i = 0; i < total_size; ++i) {
        std::allocator_traits<Allocator>::construct(allocator_, raw_allocation_ + i);
      }
    }
    
    // Adjust buffer to skip initial padding
    buffer_ = raw_allocation_ + padding;
  }

  ~AdaptiveHeapBuffer() {
    if (is_doubly_mapped_) {
      // Destroy elements in doubly mapped buffer
      if constexpr (!std::is_trivially_destructible_v<T>) {
        for (std::size_t i = 0; i < capacity_; ++i) {
          std::allocator_traits<Allocator>::destroy(allocator_, buffer_ + i);
        }
      }
      // vmem_allocation_ destructor will clean up mmap
    } else if (raw_allocation_) {
      // Standard heap buffer cleanup
      const std::size_t total_size = capacity_ + (2 * padding);
      
      // Destroy all elements including padding
      if constexpr (!std::is_trivially_destructible_v<T>) {
        for (std::size_t i = 0; i < total_size; ++i) {
          std::allocator_traits<Allocator>::destroy(allocator_, raw_allocation_ + i);
        }
      }
      std::allocator_traits<Allocator>::deallocate(allocator_, raw_allocation_, total_size);
    }
  }
  
  // Non-Copyable and Non-Movable
  AdaptiveHeapBuffer(const AdaptiveHeapBuffer &lhs) = delete;
  AdaptiveHeapBuffer &operator=(const AdaptiveHeapBuffer &lhs) = delete;
  AdaptiveHeapBuffer(AdaptiveHeapBuffer &&lhs) = delete;
  AdaptiveHeapBuffer &operator=(AdaptiveHeapBuffer &&lhs) = delete;
};

// Memory Allocated on the Stack
template <typename T, std::size_t N, typename Allocator = std::allocator<T>,
          typename = SPSC_Type<T>>
struct StackBuffer {
  // +1 prevents live lock e.g. reader and writer share 1 slot for size 1
  static constexpr std::size_t capacity_{N + 1};
  static constexpr std::size_t padding = ((cacheLineSize - 1) / sizeof(T)) + 1;
  // (2 * padding) is for preventing cache contention between adjacent memory
  std::array<T, capacity_ + (2 * padding)> buffer_;
  bool is_doubly_mapped_ = false;  // Always false for stack buffers

  explicit StackBuffer(const std::size_t capacity,
                       [[maybe_unused]] const Allocator &allocator = Allocator()) {
    if (capacity) {
      throw std::invalid_argument(
          "Capacity in constructor is ignored for stack allocations");
    }
  }

  ~StackBuffer() = default;
  // Non-Copyable and Non-Movable
  StackBuffer(const StackBuffer &lhs) = delete;
  StackBuffer &operator=(const StackBuffer &lhs) = delete;
  StackBuffer(StackBuffer &&lhs) = delete;
  StackBuffer &operator=(StackBuffer &&lhs) = delete;
};

} // namespace details

template <typename T, std::size_t N = 0,
          typename Allocator = std::allocator<T>,
          typename = details::SPSC_Type<T>,
          typename = details::MAX_STACK_SIZE<T, N>>
class SPSCQueue
    : public std::conditional_t<N == 0, details::AdaptiveHeapBuffer<T, Allocator>,
                                details::StackBuffer<T, N>> {
private:
  using base_type =
      std::conditional_t<N == 0, details::AdaptiveHeapBuffer<T, Allocator>,
                         details::StackBuffer<T, N>>;
  static constexpr bool nothrow_v = 
    std::is_nothrow_constructible_v<T> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>));

  struct alignas(details::cacheLineSize) WriterCacheLine {
    std::atomic<std::size_t> writeIndex_{0};
    std::size_t readIndexCache_{0};
  } writer_;

  struct alignas(details::cacheLineSize) ReaderCacheLine {
    std::atomic<std::size_t> readIndex_{0};
    std::size_t writeIndexCache_{0};
    // Cache capacity for performance
    std::size_t capacityCache_{};
  } reader_;

  // Helper to get buffer pointer with correct offset
  T* get_buffer_ptr() noexcept {
    if constexpr (N == 0) {
      // Heap buffer - padding already handled in buffer pointer
      return base_type::buffer_;
    } else {
      // Stack buffer - need to add padding offset
      return base_type::buffer_.data() + base_type::padding;
    }
  }

  const T* get_buffer_ptr() const noexcept {
    if constexpr (N == 0) {
      // Heap buffer - padding already handled in buffer pointer
      return base_type::buffer_;
    } else {
      // Stack buffer - need to add padding offset
      return base_type::buffer_.data() + base_type::padding;
    }
  }

public:
  explicit SPSCQueue(const std::size_t capacity = 0,
                     const Allocator &allocator = Allocator())
      : base_type(capacity, allocator) {
    reader_.capacityCache_ = base_type::capacity_;
  }

  ~SPSCQueue() = default;
  // Non-Copyable and Non-Movable
  SPSCQueue(const SPSCQueue &lhs) = delete;
  SPSCQueue &operator=(const SPSCQueue &lhs) = delete;
  SPSCQueue(SPSCQueue &&lhs) = delete;
  SPSCQueue &operator=(SPSCQueue &&lhs) = delete;

  // Blocking push - waits for space
  void push(const T &val) noexcept(nothrow_v) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    // Loop while waiting for reader to catch up
    while (nextWriteIndex == writer_.readIndexCache_) {
      writer_.readIndexCache_ =
          reader_.readIndex_.load(std::memory_order_acquire);
    }
    write_value(writeIndex, val);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  template <typename P,
            typename = std::enable_if_t<std::is_constructible_v<T, P &&>>>
  void push(P &&val) noexcept(std::is_nothrow_constructible_v<T, P &&> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    // Loop while waiting for reader to catch up
    while (nextWriteIndex == writer_.readIndexCache_) {
      writer_.readIndexCache_ =
          reader_.readIndex_.load(std::memory_order_acquire);
    }
    write_value(writeIndex, std::forward<P>(val));
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  // Non-blocking push - returns false if no space
  [[nodiscard]] bool try_push(const T &val) noexcept(nothrow_v) {
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
    write_value(writeIndex, val);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
    return true;
  }

  template <typename P,
            typename = std::enable_if_t<std::is_constructible_v<T, P &&>>>
  [[nodiscard]] bool
  try_push(P &&val) noexcept(std::is_nothrow_constructible_v<T, P &&> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
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
    write_value(writeIndex, std::forward<P>(val));
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
    return true;
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
    static_assert(std::is_trivially_copyable_v<T>, 
                  "writeN requires trivially copyable types");
    const auto capacity = base_type::capacity_;
    auto writeIndex     = writer_.writeIndex_.load(std::memory_order_relaxed);

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
    T* buffer = get_buffer_ptr();

    std::memcpy(&buffer[writeIndex], src, firstChunk * sizeof(T));

    if (firstChunk < toWrite) {
      std::memcpy(&buffer[0], src + firstChunk, (toWrite - firstChunk) * sizeof(T));
    }

    writer_.writeIndex_.store((writeIndex + toWrite) % capacity, std::memory_order_release);
    return toWrite;
  }

  std::size_t readN(T* dst, std::size_t count) noexcept(nothrow_v) {
    static_assert(std::is_trivially_copyable_v<T>, 
                  "readN requires trivially copyable types");
    const auto capacity = base_type::capacity_;

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
    const T* buffer = get_buffer_ptr();

    std::memcpy(dst, &buffer[readIndex], firstChunk * sizeof(T));

    if (firstChunk < toRead) {
      std::memcpy(dst + firstChunk, &buffer[0], (toRead - firstChunk) * sizeof(T));
    }

    const auto nextReadIndex = (readIndex + toRead) % capacity;
    reader_.readIndex_.store(nextReadIndex, std::memory_order_release);

    return toRead;
  }

  std::size_t peek_write(T*& ptr1, std::size_t& size1, T*& ptr2, std::size_t& size2) noexcept {
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
      ptr1 = nullptr;
      ptr2 = nullptr;
      size1 = 0;
      size2 = 0;
      return 0;
    }

    T* buffer = get_buffer_ptr();

    // First chunk: contiguous to end
    std::size_t first_chunk = (readIndexCache > writeIndex)
        ? space  // contiguous, no wrap
        : std::min(space, capacity - writeIndex);

    ptr1 = &buffer[writeIndex];
    size1 = first_chunk;

    if (readIndexCache <= writeIndex && first_chunk < space) {
      // Wrapped: second chunk exists
      ptr2 = &buffer[0];
      size2 = space - first_chunk;
    } else {
      ptr2 = nullptr;
      size2 = 0;
    }

    return space;
  }


  std::size_t peek_read(const T*& ptr1, std::size_t& size1, const T*& ptr2, std::size_t& size2) noexcept {
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
      ptr1 = nullptr;
      ptr2 = nullptr;
      size1 = 0;
      size2 = 0;
      return 0;
    }

    const T* buffer = get_buffer_ptr();

    // First chunk: contiguous
    std::size_t first_chunk = (writeIndexCache >= readIndex)
        ? available   // no wrap
        : std::min(available, capacity - readIndex);

    ptr1 = &buffer[readIndex];
    size1 = first_chunk;

    if (writeIndexCache < readIndex) {
      // Wrapped, so second chunk exists
      ptr2 = &buffer[0];
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

  // NEW: Zero-copy contiguous read (only available with doubly mapped heap buffers)
  std::pair<const T*, std::size_t> read_dbf() {  // Remove noexcept!
      if constexpr (N == 0) {
          // Only heap buffers can be doubly mapped
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
              
              // CRITICAL FIX: In wraparound scenarios, limit to what's safely readable
              // From readIndex, we can read at most (capacity - readIndex) before wrapping
              // But with doubly mapped buffer, the wrap is handled by the second mapping
              // However, we must not read more than what's contiguous in the logical buffer
              std::size_t max_contiguous = capacity - readIndex;
              std::size_t safe_read_size = std::min(available, max_contiguous);
              
              return {ptr, safe_read_size};
          }
          // NOT doubly mapped - throw here!
          const size_t buffer_bytes = base_type::capacity_ * sizeof(T);
          throw std::runtime_error(
              "read_dbf() requires doubly-mapped buffer. "
              "Current size: " + std::to_string(buffer_bytes) + " bytes, "
              "minimum: " + std::to_string(details::DOUBLY_MAPPED_MIN_SIZE) + " bytes."
          );
      }
      throw std::runtime_error("read_dbf() not supported for stack-allocated buffers");
  }
  // Also need commit_write (which you currently have)
  void commit_write(std::size_t count) noexcept {
      const auto capacity = base_type::capacity_;
      const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
      const auto nextWriteIndex = (writeIndex + count) % capacity;
      writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  std::pair<T*, std::size_t> write_dbf() {  // Remove noexcept!
      if constexpr (N == 0) {
          // Only heap buffers can be doubly mapped
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
              
              // With doubly mapped buffer, we can write contiguously up to capacity
              T* ptr = &base_type::buffer_[writeIndex];
              
              // CRITICAL FIX: In wraparound scenarios, limit to what's safely writable
              // From writeIndex, we can write at most (capacity - writeIndex) before wrapping
              std::size_t max_contiguous = capacity - writeIndex;
              std::size_t safe_write_size = std::min(space, max_contiguous);
              
              return {ptr, safe_write_size};
          }
          // NOT doubly mapped - throw here!
          const size_t buffer_bytes = base_type::capacity_ * sizeof(T);
          throw std::runtime_error(
              "write_dbf() requires doubly-mapped buffer. "
              "Current size: " + std::to_string(buffer_bytes) + " bytes, "
              "minimum: " + std::to_string(details::DOUBLY_MAPPED_MIN_SIZE) + " bytes."
          );
      }
      throw std::runtime_error("write_dbf() not supported for stack-allocated buffers");
  }

private:
  // Note: The padding is handled differently for heap vs stack buffers
  template <typename Index>
  T read_value(const Index &readIndex) noexcept(nothrow_v) {
    T* buffer = get_buffer_ptr();
    return std::move(buffer[readIndex]);
  }

  template <typename Index, typename U>
  void write_value(const Index &writeIndex, U &&val) noexcept(nothrow_v) {
    T* buffer = get_buffer_ptr();
    buffer[writeIndex] = std::forward<U>(val);
  }

  template <typename Index, typename... Args>
  typename std::enable_if_t<
    std::is_constructible_v<T, Args && ...> && (sizeof...(Args) > 1), void>
  write_value(const Index &writeIndex, Args &&...args) noexcept(
      std::is_nothrow_constructible_v<T, Args &&...> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    T* buffer = get_buffer_ptr();
    buffer[writeIndex] = T(std::forward<Args>(args)...);
  }
};

} // namespace dro
#endif