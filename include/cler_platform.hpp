#pragma once
#include <cstddef>

// Platform-specific includes for feature detection
#ifdef __has_include
    #if __has_include(<unistd.h>)
        #include <unistd.h>
        #define CLER_HAS_UNISTD_H 1
    #endif
    #if __has_include(<sys/mman.h>)
        #include <sys/mman.h>
        #define CLER_HAS_MMAP_H 1  
    #endif
#else
    // Conservative: assume POSIX headers exist on known POSIX systems
    #if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
        #include <unistd.h>
        #include <sys/mman.h>
        #define CLER_HAS_UNISTD_H 1
        #define CLER_HAS_MMAP_H 1
    #endif
#endif

// Additional system headers needed for doubly mapped buffer support
#if defined(CLER_HAS_MMAP_H)
    #include <fcntl.h>
    #include <errno.h>
    #include <cstdio>
    #ifdef __linux__
        #include <sys/syscall.h>
        #include <sys/types.h>
    #endif
#endif

// Windows headers for doubly mapped buffer support
#ifdef _WIN32
    #include <windows.h>
    #include <versionhelpers.h>
#endif

namespace cler {
    
    // Platform-specific cache line size detection
    namespace platform {
        #if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
        static constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
        #elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        // Intel x86/x64: 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__riscv) || defined(__riscv__)
        // RISC-V: typically 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7EM__)
        // ARM Cortex-M (STM32, TI Tiva): 32 bytes
        static constexpr std::size_t cache_line_size = 32;
        #elif defined(__ARM_ARCH) && (__ARM_ARCH >= 8)
        // ARM Cortex-A (64-bit): 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__ARM_ARCH) && (__ARM_ARCH == 7)
        // ARM Cortex-A (32-bit): typically 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__aarch64__)
        // ARM64: 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__arm__) || defined(_M_ARM)
        // Generic ARM: conservative 32 bytes
        static constexpr std::size_t cache_line_size = 32;
        #else
        // Safe default for unknown platforms
        static constexpr std::size_t cache_line_size = 64;
        #endif

        // ============= Doubly Mapped Buffer Support =============
        // Compile-time platform support check
        #if ((defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)) && \
            defined(CLER_HAS_MMAP_H) && defined(CLER_HAS_UNISTD_H)) || defined(_WIN32)
            constexpr bool has_doubly_mapped_support = true;
        #else
            constexpr bool has_doubly_mapped_support = false;
        #endif

        // Page size detection
        inline std::size_t get_page_size() {
            #if defined(_WIN32)
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                return static_cast<std::size_t>(si.dwPageSize);
            #elif defined(CLER_HAS_UNISTD_H)
                #if defined(_SC_PAGESIZE)
                    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
                #elif defined(__APPLE__)
                    return static_cast<std::size_t>(getpagesize());
                #endif
            #endif
            // Fallback to common page size
            return 4096;
        }

        // Runtime capability check with caching
        inline bool supports_doubly_mapped_buffers() {
            #if defined(_WIN32)
                // Windows 10 1809+ supports doubly-mapped buffers via VirtualAlloc2/MapViewOfFile3
                static bool tested = false;
                static bool supported = false;
                
                if (tested) return supported;
                tested = true;
                
                // Check for Windows 10 1809 or later (build 17763)
                if (IsWindows10OrGreater()) {
                    // Get actual version to check for 1809+
                    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
                    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
                    if (ntdll) {
                        RtlGetVersionPtr RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
                        if (RtlGetVersion) {
                            RTL_OSVERSIONINFOW osvi = {};
                            osvi.dwOSVersionInfoSize = sizeof(osvi);
                            if (RtlGetVersion(&osvi) == 0) {
                                // Windows 10 1809 is version 10.0.17763
                                if (osvi.dwMajorVersion > 10 || 
                                    (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 17763)) {
                                    // Check if required APIs are available
                                    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
                                    if (kernel32) {
                                        FARPROC va2 = GetProcAddress(kernel32, "VirtualAlloc2");
                                        FARPROC mv3 = GetProcAddress(kernel32, "MapViewOfFile3");
                                        supported = (va2 != nullptr) && (mv3 != nullptr);
                                        #ifdef CLER_VMEM_DEBUG
                                        if (!supported) {
                                            OutputDebugStringA("[CLER_PLATFORM] VirtualAlloc2 or MapViewOfFile3 not found\n");
                                        } else {
                                            OutputDebugStringA("[CLER_PLATFORM] VirtualAlloc2 and MapViewOfFile3 found - DBF should be supported\n");
                                        }
                                        #endif
                                    }
                                }
                            }
                        }
                    }
                }
                
                return supported;
            #elif !defined(__linux__) && !defined(__APPLE__) && !defined(__FreeBSD__)
                return false;
            #else
                static bool tested = false;
                static bool supported = false;
                
                if (tested) return supported;
                tested = true;
                
                #if defined(CLER_HAS_MMAP_H)
                    // Try to create a small test mapping
                    const size_t test_size = get_page_size();
                    int fd = -1;
                    
                    #ifdef __linux__
                        // Try memfd_create first
                        #ifdef __NR_memfd_create
                            fd = static_cast<int>(syscall(__NR_memfd_create, "cler_test", 0x0001U));
                        #endif
                        if (fd == -1) {
                            // Fallback to POSIX shm
                            fd = shm_open("/cler_test", O_CREAT | O_RDWR | O_EXCL, 0600);
                            if (fd != -1) shm_unlink("/cler_test");
                        }
                    #else  // macOS, FreeBSD
                        char name[64];
                        snprintf(name, sizeof(name), "/cler_test_%d", getpid());
                        fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
                        if (fd != -1) shm_unlink(name);
                    #endif
                    
                    if (fd != -1) {
                        if (ftruncate(fd, static_cast<off_t>(test_size)) == 0) {
                            // Reserve address space
                            void* addr = mmap(nullptr, test_size * 2, PROT_NONE, 
                                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                            if (addr != MAP_FAILED) {
                                // Try first mapping
                                void* m1 = mmap(addr, test_size, PROT_READ | PROT_WRITE,
                                               MAP_SHARED | MAP_FIXED, fd, 0);
                                if (m1 != MAP_FAILED) {
                                    // Try second mapping
                                    void* m2 = mmap(static_cast<char*>(addr) + test_size, 
                                                   test_size, PROT_READ | PROT_WRITE,
                                                   MAP_SHARED | MAP_FIXED, fd, 0);
                                    if (m2 != MAP_FAILED) {
                                        // Test that both mappings see the same data
                                        *static_cast<int*>(m1) = 0x12345678;
                                        supported = (*static_cast<int*>(m2) == 0x12345678);
                                    }
                                }
                                munmap(addr, test_size * 2);
                            }
                        }
                        close(fd);
                    }
                #endif
                
                return supported;
            #endif
        }
    }
}