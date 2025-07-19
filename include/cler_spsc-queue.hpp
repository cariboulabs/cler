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
#include <new>         // for std::hardware_destructive_interference_size
#include <stdexcept>   // for std::logic_error
#include <type_traits> // for std::is_default_constructible
#include <utility>     // for forward
#include <cstring>    // for std::memcpy

namespace dro {

namespace details {

// Platform-aware cache line size detection
#if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
static constexpr std::size_t cacheLineSize = std::hardware_destructive_interference_size;
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
// Intel x86/x64: 64 bytes
static constexpr std::size_t cacheLineSize = 64;
#elif defined(__riscv) || defined(__riscv__)
static constexpr std::size_t cacheLineSize = 64; //most are 64
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

// Memory Allocated on the Heap (Default Option)
template <typename T, typename Allocator = std::allocator<T>,
          typename = SPSC_Type<T>>
struct HeapBuffer {
  const std::size_t capacity_;
  T* buffer_;
  [[no_unique_address]] Allocator allocator_;

  static constexpr std::size_t padding = ((cacheLineSize - 1) / sizeof(T)) + 1;
  static constexpr std::size_t MAX_SIZE_T =
      std::numeric_limits<std::size_t>::max();

  explicit HeapBuffer(const std::size_t capacity,
                      const Allocator &allocator = Allocator())
      // +1 prevents live lock e.g. reader and writer share 1 slot for size 1
      : capacity_(capacity + 1), allocator_(allocator) {
    if (capacity < 1) {
      throw std::logic_error("Capacity must be a positive number; Heap "
                             "allocations require capacity argument");
    }
    // (2 * padding) is for preventing cache contention between adjacent memory
    if (capacity_ > MAX_SIZE_T - (2 * padding)) {
      throw std::overflow_error(
          "Capacity with padding exceeds std::size_t. Reduce size of queue.");
    }
    
    const std::size_t total_size = capacity_ + (2 * padding);
    buffer_ = std::allocator_traits<Allocator>::allocate(allocator_, total_size);
    
    // Initialize elements if T is not trivially constructible
    if constexpr (!std::is_trivially_constructible_v<T>) {
      for (std::size_t i = 0; i < total_size; ++i) {
        std::allocator_traits<Allocator>::construct(allocator_, buffer_ + i);
      }
    }
  }

  ~HeapBuffer() {
    if (buffer_) {
      const std::size_t total_size = capacity_ + (2 * padding);
      // Destroy elements if T is not trivially destructible
      if constexpr (!std::is_trivially_destructible_v<T>) {
        for (std::size_t i = 0; i < total_size; ++i) {
          std::allocator_traits<Allocator>::destroy(allocator_, buffer_ + i);
        }
      }
      std::allocator_traits<Allocator>::deallocate(allocator_, buffer_, total_size);
    }
  }
  
  // Non-Copyable and Non-Movable
  HeapBuffer(const HeapBuffer &lhs) = delete;
  HeapBuffer &operator=(const HeapBuffer &lhs) = delete;
  HeapBuffer(HeapBuffer &&lhs) = delete;
  HeapBuffer &operator=(HeapBuffer &&lhs) = delete;
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
    : public std::conditional_t<N == 0, details::HeapBuffer<T, Allocator>,
                                details::StackBuffer<T, N>> {
private:
  using base_type =
      std::conditional_t<N == 0, details::HeapBuffer<T, Allocator>,
                         details::StackBuffer<T, N>>;
  static constexpr bool nothrow_v = 
    std::is_nothrow_constructible_v<T> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>));

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

  ~SPSCQueue() = default;
  // Non-Copyable and Non-Movable
  SPSCQueue(const SPSCQueue &lhs) = delete;
  SPSCQueue &operator=(const SPSCQueue &lhs) = delete;
  SPSCQueue(SPSCQueue &&lhs) = delete;
  SPSCQueue &operator=(SPSCQueue &&lhs) = delete;

  template <typename... Args,
            typename = std::enable_if_t<std::is_constructible_v<T, Args &&...>>>
  void
  emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args &&...> &&
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
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  template <typename... Args,
            typename = std::enable_if_t<std::is_constructible_v<T, Args &&...>>>
  void force_emplace(Args &&...args) noexcept(
      std::is_nothrow_constructible_v<T, Args &&...> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    const auto writeIndex = writer_.writeIndex_.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        (writeIndex == base_type::capacity_ - 1) ? 0 : writeIndex + 1;
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
  }

  template <typename... Args,
            typename = std::enable_if_t<std::is_constructible_v<T, Args &&...>>>
  [[nodiscard]] bool try_emplace(Args &&...args) noexcept(
      std::is_nothrow_constructible_v<T, Args &&...> &&
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
    write_value(writeIndex, std::forward<Args>(args)...);
    writer_.writeIndex_.store(nextWriteIndex, std::memory_order_release);
    return true;
  }

  void push(const T &val) noexcept(nothrow_v) { emplace(val); }

  template <typename P,
            typename = std::enable_if_t<std::is_constructible_v<T, P &&>>>
  void push(P &&val) noexcept(std::is_nothrow_constructible_v<T, P &&> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    emplace(std::forward<P>(val));
  }

  void force_push(const T &val) noexcept(nothrow_v) { force_emplace(val); }

  template <typename P,
            typename = std::enable_if_t<std::is_constructible_v<T, P &&>>>
  void force_push(P &&val) noexcept(std::is_nothrow_constructible_v<T, P &&> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    force_emplace(std::forward<P>(val));
  }

  [[nodiscard]] bool try_push(const T &val) noexcept(nothrow_v) {
    return try_emplace(val);
  }

  template <typename P,
            typename = std::enable_if_t<std::is_constructible_v<T, P &&>>>
  [[nodiscard]] bool
  try_push(P &&val) noexcept(std::is_nothrow_constructible_v<T, P &&> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
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
  template <typename Index>
  T read_value(const Index &readIndex) noexcept(nothrow_v) {
    return std::move(base_type::buffer_[readIndex + base_type::padding]);
  }

  template <typename Index, typename U>
  void write_value(const Index &writeIndex, U &&val) noexcept(nothrow_v) {
    base_type::buffer_[writeIndex + writer_.paddingCache_] = std::forward<U>(val);
  }

  template <typename Index, typename... Args>
  typename std::enable_if_t<
    std::is_constructible_v<T, Args && ...> && (sizeof...(Args) > 1), void>
  write_value(const Index &writeIndex, Args &&...args) noexcept(
      std::is_nothrow_constructible_v<T, Args &&...> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>))) {
    base_type::buffer_[writeIndex + writer_.paddingCache_] = T(std::forward<Args>(args)...);
  }
};

} // namespace dro
#endif
