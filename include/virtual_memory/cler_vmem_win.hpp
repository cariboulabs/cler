#pragma once

#ifdef _WIN32

#include <windows.h>
#include <memoryapi.h>
#include <sysinfoapi.h>
#include <errhandlingapi.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include "../cler_platform.hpp"

// Windows 10+ memory management APIs
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#error "This implementation requires Windows 10 or later"
#endif

// Debug diagnostics - define CLER_VMEM_DEBUG to enable
#ifdef CLER_VMEM_DEBUG
    #define VMEM_LOG(fmt, ...) do { \
        char buf[512]; \
        snprintf(buf, sizeof(buf), "[CLER_VMEM] " fmt "\n", ##__VA_ARGS__); \
        OutputDebugStringA(buf); \
    } while(0)
#else
    #define VMEM_LOG(fmt, ...) ((void)0)
#endif

namespace cler {
namespace vmem {

// Windows implementation of doubly mapped buffer
class DoublyMappedAllocation {
private:
    HANDLE file_mapping_ = INVALID_HANDLE_VALUE;
    LPVOID base_address_ = nullptr;
    SIZE_T mapping_size_ = 0;
    bool is_valid_ = false;
    bool using_large_pages_ = false;
    bool used_placeholders_ = false;

public:
    DoublyMappedAllocation() = default;
    
    // Non-copyable, movable
    DoublyMappedAllocation(const DoublyMappedAllocation&) = delete;
    DoublyMappedAllocation& operator=(const DoublyMappedAllocation&) = delete;
    
    DoublyMappedAllocation(DoublyMappedAllocation&& other) noexcept
        : file_mapping_(other.file_mapping_)
        , base_address_(other.base_address_)
        , mapping_size_(other.mapping_size_)
        , is_valid_(other.is_valid_)
        , using_large_pages_(other.using_large_pages_)
        , used_placeholders_(other.used_placeholders_) {
        other.file_mapping_ = INVALID_HANDLE_VALUE;
        other.base_address_ = nullptr;
        other.mapping_size_ = 0;
        other.is_valid_ = false;
        other.using_large_pages_ = false;
        other.used_placeholders_ = false;
    }
    
    DoublyMappedAllocation& operator=(DoublyMappedAllocation&& other) noexcept {
        if (this != &other) {
            cleanup();
            file_mapping_ = other.file_mapping_;
            base_address_ = other.base_address_;
            mapping_size_ = other.mapping_size_;
            is_valid_ = other.is_valid_;
            using_large_pages_ = other.using_large_pages_;
            used_placeholders_ = other.used_placeholders_;
            other.file_mapping_ = INVALID_HANDLE_VALUE;
            other.base_address_ = nullptr;
            other.mapping_size_ = 0;
            other.is_valid_ = false;
            other.using_large_pages_ = false;
            other.used_placeholders_ = false;
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
        
        // Get system info
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const SIZE_T page_size = si.dwPageSize;
        SIZE_T aligned_size = ((size + page_size - 1) / page_size) * page_size;
        
        // Try large pages if available and size is sufficient
        SIZE_T large_page_size = GetLargePageMinimum();
        bool try_large_pages = false;
        
        if (large_page_size > 0 && aligned_size >= large_page_size) {
            // Align to large page boundary
            aligned_size = ((size + large_page_size - 1) / large_page_size) * large_page_size;
            try_large_pages = enable_large_page_privilege();
            VMEM_LOG("Large pages %s (size: %zu)", 
                     try_large_pages ? "enabled" : "failed to enable", 
                     large_page_size);
        }
        
        // First attempt: Try using VirtualAlloc2 with placeholders (Windows 10 RS5+)
        if (try_with_placeholders(aligned_size, try_large_pages)) {
            return true;
        }
        
        // Second attempt: Traditional approach with MapViewOfFileEx
        if (try_with_map_view(aligned_size, try_large_pages)) {
            return true;
        }
        
        // If large pages were requested and failed, retry without them
        if (try_large_pages) {
            // Recalculate alignment for regular pages
            aligned_size = ((size + page_size - 1) / page_size) * page_size;
            
            if (try_with_placeholders(aligned_size, false)) {
                return true;
            }
            
            return try_with_map_view(aligned_size, false);
        }
        
        return false;
    }
    
    // Get the base pointer (first mapping)
    void* data() const {
        return is_valid_ ? base_address_ : nullptr;
    }
    
    // Get the second mapping pointer
    void* second_mapping() const {
        return is_valid_ ? static_cast<char*>(base_address_) + mapping_size_ : nullptr;
    }
    
    // Get the size of each mapping
    std::size_t size() const {
        return is_valid_ ? mapping_size_ : 0;
    }
    
    bool valid() const {
        return is_valid_;
    }

private:
    void cleanup() {
        if (base_address_) {
            // Unmap both views
            UnmapViewOfFile(base_address_);
            UnmapViewOfFile(static_cast<char*>(base_address_) + mapping_size_);
            
            // Only free virtual allocation if we used placeholders
            if (used_placeholders_) {
                VirtualFree(base_address_, 0, MEM_RELEASE);
            }
        }
        
        if (file_mapping_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_mapping_);
        }
        
        base_address_ = nullptr;
        mapping_size_ = 0;
        file_mapping_ = INVALID_HANDLE_VALUE;
        is_valid_ = false;
        using_large_pages_ = false;
        used_placeholders_ = false;
    }
    
    bool enable_large_page_privilege() {
        HANDLE token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) {
            return false;
        }
        
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        
        if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &tp.Privileges[0].Luid)) {
            CloseHandle(token);
            return false;
        }
        
        BOOL result = AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        DWORD error = GetLastError();
        CloseHandle(token);
        
        return result && error == ERROR_SUCCESS;
    }
    
    // Modern approach using VirtualAlloc2 with placeholders (Windows 10 1809+)
    bool try_with_placeholders(SIZE_T aligned_size, bool use_large_pages) {
        // Try to load VirtualAlloc2 dynamically (available in Windows 10 1809+)
        typedef PVOID (WINAPI *PVirtualAlloc2)(
            HANDLE Process,
            PVOID BaseAddress,
            SIZE_T Size,
            ULONG AllocationType,
            ULONG PageProtection,
            MEM_EXTENDED_PARAMETER* ExtendedParameters,
            ULONG ParameterCount
        );
        
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32) return false;
        
        PVirtualAlloc2 pVirtualAlloc2 = (PVirtualAlloc2)GetProcAddress(kernel32, "VirtualAlloc2");
        if (!pVirtualAlloc2) return false;
        
        // Reserve address space for both mappings
        DWORD alloc_flags = MEM_RESERVE | MEM_RESERVE_PLACEHOLDER;
        if (use_large_pages) {
            alloc_flags |= MEM_LARGE_PAGES;
        }
        
        LPVOID placeholder = pVirtualAlloc2(
            GetCurrentProcess(),
            NULL,
            aligned_size * 2,
            alloc_flags,
            PAGE_NOACCESS,
            NULL,
            0
        );
        
        if (!placeholder) {
            return false;
        }
        
        // Split the placeholder into two regions
        if (!VirtualFree(placeholder, aligned_size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
            VirtualFree(placeholder, 0, MEM_RELEASE);
            return false;
        }
        
        // Create file mapping
        DWORD protect = PAGE_READWRITE | SEC_COMMIT;
        if (use_large_pages) {
            protect |= SEC_LARGE_PAGES;
        }
        
        file_mapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            protect,
            0,
            static_cast<DWORD>(aligned_size),
            NULL
        );
        
        if (file_mapping_ == INVALID_HANDLE_VALUE) {
            VirtualFree(placeholder, 0, MEM_RELEASE);
            VirtualFree(static_cast<char*>(placeholder) + aligned_size, 0, MEM_RELEASE);
            return false;
        }
        
        // Map the file to both placeholders
        LPVOID first_mapping = MapViewOfFile3(
            file_mapping_,
            GetCurrentProcess(),
            placeholder,
            0,
            aligned_size,
            MEM_REPLACE_PLACEHOLDER,
            PAGE_READWRITE,
            NULL,
            0
        );
        
        if (!first_mapping) {
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            VirtualFree(placeholder, 0, MEM_RELEASE);
            VirtualFree(static_cast<char*>(placeholder) + aligned_size, 0, MEM_RELEASE);
            return false;
        }
        
        LPVOID second_mapping = MapViewOfFile3(
            file_mapping_,
            GetCurrentProcess(),
            static_cast<char*>(placeholder) + aligned_size,
            0,
            aligned_size,
            MEM_REPLACE_PLACEHOLDER,
            PAGE_READWRITE,
            NULL,
            0
        );
        
        if (!second_mapping) {
            UnmapViewOfFile(first_mapping);
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            // Need to free both placeholders on failure
            VirtualFree(placeholder, 0, MEM_RELEASE);
            VirtualFree(static_cast<char*>(placeholder) + aligned_size, 0, MEM_RELEASE);
            return false;
        }
        
        // Verify the double mapping actually works
        volatile char* first_byte = static_cast<char*>(first_mapping);
        volatile char* second_byte = static_cast<char*>(second_mapping);
        
        // Write to first mapping
        *first_byte = 42;
        
        // Check if it appears in second mapping
        if (*second_byte != 42) {
            // Double mapping failed!
            UnmapViewOfFile(first_mapping);
            UnmapViewOfFile(second_mapping);
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            VMEM_LOG("Double mapping verification failed!");
            return false;
        }
        
        // Clean up test
        *first_byte = 0;
        
        base_address_ = first_mapping;
        mapping_size_ = aligned_size;
        using_large_pages_ = use_large_pages;
        used_placeholders_ = true;  // Mark that we used the placeholder approach
        is_valid_ = true;
        
        VMEM_LOG("Double mapping created successfully at %p, size: %zu", base_address_, mapping_size_);
        
        return true;
    }
    
    // Traditional approach using MapViewOfFileEx
    bool try_with_map_view(SIZE_T aligned_size, bool use_large_pages) {
        // Create file mapping
        DWORD protect = PAGE_READWRITE;
        if (use_large_pages) {
            protect |= SEC_LARGE_PAGES;
        }
        
        file_mapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            protect | SEC_COMMIT,
            0,
            static_cast<DWORD>(aligned_size),
            NULL
        );
        
        if (file_mapping_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        // Reserve address space for both mappings
        LPVOID reserved = VirtualAlloc(
            NULL,
            aligned_size * 2,
            MEM_RESERVE,
            PAGE_NOACCESS
        );
        
        if (!reserved) {
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            return false;
        }
        
        // Free the reservation (we just wanted the address)
        VirtualFree(reserved, 0, MEM_RELEASE);
        
        // Try to map at the reserved addresses
        LPVOID first_mapping = MapViewOfFileEx(
            file_mapping_,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            aligned_size,
            reserved
        );
        
        if (!first_mapping) {
            // Try without a specific address
            first_mapping = MapViewOfFile(
                file_mapping_,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                aligned_size
            );
            
            if (!first_mapping) {
                CloseHandle(file_mapping_);
                file_mapping_ = INVALID_HANDLE_VALUE;
                return false;
            }
        }
        
        // Try to map the second view immediately after the first
        LPVOID second_mapping = MapViewOfFileEx(
            file_mapping_,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            aligned_size,
            static_cast<char*>(first_mapping) + aligned_size
        );
        
        if (!second_mapping) {
            // If we can't get contiguous mappings, give up
            UnmapViewOfFile(first_mapping);
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            return false;
        }
        
        // Verify the double mapping actually works
        volatile char* first_byte = static_cast<char*>(first_mapping);
        volatile char* second_byte = static_cast<char*>(second_mapping);
        
        // Write to first mapping
        *first_byte = 42;
        
        // Check if it appears in second mapping
        if (*second_byte != 42) {
            // Double mapping failed!
            UnmapViewOfFile(first_mapping);
            UnmapViewOfFile(second_mapping);
            CloseHandle(file_mapping_);
            file_mapping_ = INVALID_HANDLE_VALUE;
            VMEM_LOG("Double mapping verification failed!");
            return false;
        }
        
        // Clean up test
        *first_byte = 0;
        
        base_address_ = first_mapping;
        mapping_size_ = aligned_size;
        using_large_pages_ = use_large_pages;
        is_valid_ = true;
        
        VMEM_LOG("Double mapping created successfully at %p, size: %zu", base_address_, mapping_size_);
        
        return true;
    }
    
    // Helper to dynamically load MapViewOfFile3 (Windows 10 1809+)
    LPVOID MapViewOfFile3(
        HANDLE FileMapping,
        HANDLE Process,
        PVOID BaseAddress,
        ULONG64 Offset,
        SIZE_T ViewSize,
        ULONG AllocationType,
        ULONG PageProtection,
        MEM_EXTENDED_PARAMETER* ExtendedParameters,
        ULONG ParameterCount
    ) {
        typedef PVOID (WINAPI *PMapViewOfFile3)(
            HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, 
            MEM_EXTENDED_PARAMETER*, ULONG
        );
        
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32) return nullptr;
        
        PMapViewOfFile3 pMapViewOfFile3 = (PMapViewOfFile3)GetProcAddress(kernel32, "MapViewOfFile3");
        if (!pMapViewOfFile3) return nullptr;
        
        return pMapViewOfFile3(
            FileMapping, Process, BaseAddress, Offset, ViewSize,
            AllocationType, PageProtection, ExtendedParameters, ParameterCount
        );
    }
};

} // namespace vmem
} // namespace cler

#endif // _WIN32