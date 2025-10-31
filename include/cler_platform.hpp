#pragma once
#include <cstddef>
#include <ctime>     // for time() in shm_open unique naming
#include <cstdio>    // for snprintf()

// ---- Defaults (avoid undefined macro checks) ----
#ifndef CLER_HAS_UNISTD_H
#define CLER_HAS_UNISTD_H 0
#endif
#ifndef CLER_HAS_MMAP_H
#define CLER_HAS_MMAP_H 0
#endif

// Platform-specific includes for feature detection
#ifdef __has_include
    #if __has_include(<unistd.h>)
        #include <unistd.h>
        #undef  CLER_HAS_UNISTD_H
        #define CLER_HAS_UNISTD_H 1
    #endif
    #if __has_include(<sys/mman.h>)
        #include <sys/mman.h>
        #undef  CLER_HAS_MMAP_H
        #define CLER_HAS_MMAP_H 1
    #endif
#else
    // Conservative: assume POSIX headers exist on known POSIX systems
    #if defined(__unix__) || defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__)
        #include <unistd.h>
        #include <sys/mman.h>
        #undef  CLER_HAS_UNISTD_H
        #define CLER_HAS_UNISTD_H 1
        #undef  CLER_HAS_MMAP_H
        #define CLER_HAS_MMAP_H 1
    #endif
#endif

// macOS and FreeBSD MAP_ANONYMOUS compatibility
#if (defined(__APPLE__) || defined(__FreeBSD__)) && !defined(MAP_ANONYMOUS)
    #define MAP_ANONYMOUS MAP_ANON
#endif

// Additional system headers needed for doubly mapped buffer support
#if CLER_HAS_MMAP_H
    #include <fcntl.h>
    #include <errno.h>
    #ifdef __linux__
        #include <sys/syscall.h>
        #ifdef __has_include
            #if __has_include(<linux/memfd.h>)
                #include <linux/memfd.h>
            #endif
        #endif
    #endif
#endif

// Thread affinity headers (desktop only)
#ifdef __linux__
    #include <pthread.h>
    #include <sched.h>
#endif

// x86 intrinsics for pause instruction
#if defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
#endif


namespace cler {
    
    // Platform-specific cache line size detection
    namespace platform {
        #if defined(__cpp_lib_hardware_interference_size) && __cpp_lib_hardware_interference_size >= 201703L
        static constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
        #elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        // Intel x86/x64: 64 bytes
        static constexpr std::size_t cache_line_size = 64;
        #elif defined(__riscv)
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
        #if (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)) && \
            CLER_HAS_MMAP_H && CLER_HAS_UNISTD_H
            constexpr bool has_doubly_mapped_support = true;
        #else
            constexpr bool has_doubly_mapped_support = false;
        #endif

        // Page size detection with caching
        inline std::size_t get_page_size() {
            static std::size_t ps = [] {
                #if CLER_HAS_UNISTD_H
                    #if defined(_SC_PAGESIZE)
                        long v = sysconf(_SC_PAGESIZE);
                        return v > 0 ? static_cast<std::size_t>(v) : 4096u;
                    #else
                        return static_cast<std::size_t>(getpagesize());
                    #endif
                #else
                    return 4096u;
                #endif
            }();
            return ps;
        }

        // Runtime capability check with caching
        inline bool supports_doubly_mapped_buffers() {
            #if !defined(__linux__) && !defined(__APPLE__) && !defined(__FreeBSD__)
                return false;
            #else
                static bool tested = false;
                static bool supported = false;
                
                if (tested) return supported;
                tested = true;
                
                #if CLER_HAS_MMAP_H
                    // Try to create a small test mapping
                    const size_t test_size = get_page_size();
                    int fd = -1;
                    
                    #ifdef __linux__
                        // Try memfd_create first with proper flags
                        #ifdef __NR_memfd_create
                            #ifndef MFD_CLOEXEC
                                #define MFD_CLOEXEC 0x0001U
                            #endif
                            #ifndef MFD_ALLOW_SEALING
                                #define MFD_ALLOW_SEALING 0x0002U
                            #endif
                            fd = static_cast<int>(syscall(__NR_memfd_create, "cler_dbuf_probe",
                                                        MFD_CLOEXEC | MFD_ALLOW_SEALING));
                        #endif
                        if (fd == -1) {
                            // Fallback to POSIX shm with unique name
                            char name[64];
                            for (int attempt = 0; attempt < 8; ++attempt) {
                                snprintf(name, sizeof(name), "/cler_dbuf_%d_%ld_%d",
                                        getpid(), time(nullptr), attempt);
                                fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
                                if (fd != -1) {
                                    shm_unlink(name);
                                    break;
                                }
                                if (errno != EEXIST) break;
                            }
                        }
                    #else  // macOS, FreeBSD
                        char name[64];
                        for (int attempt = 0; attempt < 8; ++attempt) {
                            snprintf(name, sizeof(name), "/cler_dbuf_%d_%ld_%d",
                                    getpid(), time(nullptr), attempt);
                            fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
                            if (fd != -1) {
                                shm_unlink(name);
                                break;
                            }
                            if (errno != EEXIST) break;
                        }
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
                                        // Test that both mappings see the same data (volatile to prevent optimization)
                                        *static_cast<volatile int*>(m1) = 0x12345678;
                                        supported = (*static_cast<volatile int*>(m2) == 0x12345678);
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

        // ============= Platform-Specific Performance Helpers =============
        // These are actually used by the desktop task policy
        
        // Simple spin-wait for backward compatibility
        inline void spin_wait(size_t iterations = 64) {
            for (size_t i = 0; i < iterations; ++i) {
                #if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__) || defined(__arm__)
                    asm volatile("yield" ::: "memory");
                #elif defined(__riscv)
                    asm volatile("" ::: "memory"); // compiler barrier only
                #else
                    asm volatile("" ::: "memory");
                #endif
            }
        }

        // Set thread affinity to a specific CPU core (desktop only)
        inline bool set_thread_affinity(std::size_t core_id) {
            #if defined(__linux__)
                cpu_set_t set;
                CPU_ZERO(&set);
                CPU_SET(core_id, &set);
                return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
            #else
                (void)core_id;
                return false;
            #endif
        }
    }
}