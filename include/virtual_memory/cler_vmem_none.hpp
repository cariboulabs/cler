#pragma once

// Fallback implementation for platforms without doubly mapped buffer support

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
    
    // Always fails on unsupported platforms
    bool create(std::size_t size) {
        (void)size;
        return false;
    }
    
    // Always returns null
    void* data() const {
        return nullptr;
    }
    
    // Always returns null  
    void* second_mapping() const {
        return nullptr;
    }
    
    // Always returns 0
    std::size_t size() const {
        return 0;
    }
    
    // Always false
    bool valid() const {
        return false;
    }
};

} // namespace vmem
} // namespace cler