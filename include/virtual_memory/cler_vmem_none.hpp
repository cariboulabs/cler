#pragma once
#include <stdexcept>
#include <cstddef>

// Fallback implementation for platforms without doubly mapped buffer support
// Throws exceptions to give users clear feedback about missing support

namespace cler {
namespace vmem {

// Stub implementation that always fails
class DoublyMappedAllocation {
public:
    DoublyMappedAllocation() = default;
    
    // Non-copyable, movable
    DoublyMappedAllocation(const DoublyMappedAllocation&) = delete;
    DoublyMappedAllocation& operator=(const DoublyMappedAllocation&) = delete;
    
    DoublyMappedAllocation(DoublyMappedAllocation&& other) noexcept {
        // Nothing to move for stub implementation
        (void)other;
    }
    
    DoublyMappedAllocation& operator=(DoublyMappedAllocation&& other) noexcept {
        // Nothing to move for stub implementation
        (void)other;
        return *this;
    }
    
    ~DoublyMappedAllocation() = default;
    
    // Throws exception to clearly indicate missing platform support
    bool create(std::size_t size) {
        (void)size;
        throw std::runtime_error(
            "Doubly mapped buffers are not supported on this platform. "
            "Available on Linux, macOS, and FreeBSD only. "
            "Use standard heap buffers or compile on a supported platform."
        );
    }
    
    // Throws exception to clearly indicate missing platform support
    void* data() const {
        throw std::runtime_error("Doubly mapped buffers not supported on this platform");
    }
    
    // Throws exception to clearly indicate missing platform support
    void* second_mapping() const {
        throw std::runtime_error("Doubly mapped buffers not supported on this platform");
    }
    
    // Throws exception to clearly indicate missing platform support
    std::size_t size() const {
        throw std::runtime_error("Doubly mapped buffers not supported on this platform");
    }
    
    // Throws exception to clearly indicate missing platform support
    bool valid() const {
        throw std::runtime_error("Doubly mapped buffers not supported on this platform");
    }
};

} // namespace vmem
} // namespace cler