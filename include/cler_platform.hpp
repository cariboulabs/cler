#pragma once
#include <cstddef>

namespace cler {
    
    // Platform-specific cache line size detection
    namespace detail {
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
    }
}