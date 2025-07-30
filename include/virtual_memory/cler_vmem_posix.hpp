#pragma once

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include "../cler_platform.hpp"

#ifdef __linux__
    #include <sys/syscall.h>
    // Define memfd_create if not available in headers
    #ifndef MFD_CLOEXEC
        #define MFD_CLOEXEC 0x0001U
    #endif
    #ifdef __NR_memfd_create
        static inline int memfd_create_wrapper(const char *name, unsigned int flags) {
            return static_cast<int>(syscall(__NR_memfd_create, name, flags));
        }
    #endif
#endif

namespace cler {
namespace vmem {

// POSIX implementation of doubly mapped buffer
class DoublyMappedAllocation {
private:
    void* mmap_base_ = nullptr;
    std::size_t mmap_size_ = 0;
    int shm_fd_ = -1;
    bool is_valid_ = false;

public:
    DoublyMappedAllocation() = default;
    
    // Non-copyable, movable
    DoublyMappedAllocation(const DoublyMappedAllocation&) = delete;
    DoublyMappedAllocation& operator=(const DoublyMappedAllocation&) = delete;
    
    DoublyMappedAllocation(DoublyMappedAllocation&& other) noexcept
        : mmap_base_(other.mmap_base_)
        , mmap_size_(other.mmap_size_)
        , shm_fd_(other.shm_fd_)
        , is_valid_(other.is_valid_) {
        other.mmap_base_ = nullptr;
        other.mmap_size_ = 0;
        other.shm_fd_ = -1;
        other.is_valid_ = false;
    }
    
    DoublyMappedAllocation& operator=(DoublyMappedAllocation&& other) noexcept {
        if (this != &other) {
            cleanup();
            mmap_base_ = other.mmap_base_;
            mmap_size_ = other.mmap_size_;
            shm_fd_ = other.shm_fd_;
            is_valid_ = other.is_valid_;
            other.mmap_base_ = nullptr;
            other.mmap_size_ = 0;
            other.shm_fd_ = -1;
            other.is_valid_ = false;
        }
        return *this;
    }
    
    ~DoublyMappedAllocation() {
        cleanup();
    }
    
    // Try to create doubly mapped allocation of specified size
    bool create(std::size_t size) {
        if (is_valid_) {
            cleanup();
        }
        
        // Get page sizes
        const std::size_t page_size = cler::platform::get_page_size();
        std::size_t aligned_size = ((size + page_size - 1) / page_size) * page_size;
        
        // Determine if we should try huge pages
        bool use_huge_pages = false;
        
        #ifdef MAP_HUGETLB
        const std::size_t huge_page_size = get_huge_page_size();
        if (huge_page_size > 0 && aligned_size >= huge_page_size) {
            // Align to huge page boundary
            aligned_size = ((size + huge_page_size - 1) / huge_page_size) * huge_page_size;
            use_huge_pages = true;
        }
        #endif
        
        // Create shared memory file descriptor
        shm_fd_ = create_shared_memory();
        if (shm_fd_ == -1) {
            return false;
        }
        
        // Set size of shared memory
        if (ftruncate(shm_fd_, static_cast<off_t>(aligned_size)) == -1) {
            close(shm_fd_);
            shm_fd_ = -1;
            return false;
        }
        
        // Prepare mmap flags
        int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
        
        #ifdef MAP_HUGETLB
        if (use_huge_pages) {
            mmap_flags |= MAP_HUGETLB;
            
            // Explicitly specify huge page size if available
            #ifdef MAP_HUGE_2MB
            if (huge_page_size == 2 * 1024 * 1024) {
                mmap_flags |= MAP_HUGE_2MB;
            }
            #endif
            #ifdef MAP_HUGE_1GB
            if (huge_page_size == 1024 * 1024 * 1024) {
                mmap_flags |= MAP_HUGE_1GB;
            }
            #endif
        }
        #endif
        
        // Reserve address space for both mappings
        void* addr_space = mmap(nullptr, aligned_size * 2, 
                               PROT_NONE, mmap_flags, -1, 0);
                               
        // If huge pages failed, try without
        if (addr_space == MAP_FAILED && use_huge_pages) {
            mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
            addr_space = mmap(nullptr, aligned_size * 2, 
                             PROT_NONE, mmap_flags, -1, 0);
        }
        
        if (addr_space == MAP_FAILED) {
            close(shm_fd_);
            shm_fd_ = -1;
            return false;
        }
        
        // Create first mapping
        void* first = mmap(addr_space, aligned_size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_FIXED, shm_fd_, 0);
        if (first == MAP_FAILED) {
            munmap(addr_space, aligned_size * 2);
            close(shm_fd_);
            shm_fd_ = -1;
            return false;
        }
        
        // Create second mapping immediately after first
        void* second = mmap(static_cast<char*>(addr_space) + aligned_size, 
                           aligned_size,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_FIXED, shm_fd_, 0);
        if (second == MAP_FAILED) {
            munmap(addr_space, aligned_size * 2);
            close(shm_fd_);
            shm_fd_ = -1;
            return false;
        }
        
        mmap_base_ = addr_space;
        mmap_size_ = aligned_size;
        is_valid_ = true;
        
        return true;
    }
    
    // Get the base pointer (first mapping)
    void* data() const {
        return is_valid_ ? mmap_base_ : nullptr;
    }
    
    // Get the second mapping pointer
    void* second_mapping() const {
        return is_valid_ ? static_cast<char*>(mmap_base_) + mmap_size_ : nullptr;
    }
    
    // Get the size of each mapping
    std::size_t size() const {
        return is_valid_ ? mmap_size_ : 0;
    }
    
    bool valid() const {
        return is_valid_;
    }

private:
    void cleanup() {
        if (mmap_base_ && mmap_base_ != MAP_FAILED) {
            munmap(mmap_base_, mmap_size_ * 2);
        }
        if (shm_fd_ != -1) {
            close(shm_fd_);
        }
        mmap_base_ = nullptr;
        mmap_size_ = 0;
        shm_fd_ = -1;
        is_valid_ = false;
    }
    
    int create_shared_memory() {
        #ifdef __linux__
            // Try memfd_create first (Linux 3.17+)
            #ifdef __NR_memfd_create
                int fd = memfd_create_wrapper("cler_buffer", MFD_CLOEXEC);
                if (fd != -1) {
                    return fd;
                }
            #endif
            
            // Fallback to POSIX shm_open
            char name[64];
            auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            snprintf(name, sizeof(name), "/cler_%d_%lld", getpid(), 
                     static_cast<long long>(now));
            fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
            if (fd != -1) {
                shm_unlink(name);  // Immediately unlink to make it anonymous
            }
            return fd;
            
        #else  // macOS, FreeBSD
            char name[64];
            auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            snprintf(name, sizeof(name), "/cler_%d_%lld", getpid(), 
                     static_cast<long long>(now));
            int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
            if (fd != -1) {
                shm_unlink(name);  // Immediately unlink to make it anonymous
            }
            return fd;
        #endif
    }
    
    static std::size_t get_huge_page_size() {
        #ifdef __linux__
        // First try sysconf if available
        #ifdef _SC_LARGE_PAGESIZE
        long size = sysconf(_SC_LARGE_PAGESIZE);
        if (size > 0) return static_cast<std::size_t>(size);
        #endif
        
        // Fallback to parsing /proc/meminfo
        FILE* f = fopen("/proc/meminfo", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                std::size_t size_kb;
                if (sscanf(line, "Hugepagesize: %zu kB", &size_kb) == 1) {
                    fclose(f);
                    return size_kb * 1024;
                }
            }
            fclose(f);
        }
        #endif
        return 0;  // No huge pages available
    }
};

} // namespace vmem
} // namespace cler

#endif // defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)