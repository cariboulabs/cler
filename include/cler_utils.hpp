#pragma once

// Cross-platform utilities for CLER framework
// These work on both desktop and embedded systems

#include "cler.hpp"

namespace cler {

// Fast bit manipulation utility - finds largest power of 2 <= x
// Useful for buffer sizing and alignment calculations
[[maybe_unused]] inline size_t floor2p2(size_t x) {
    if (x == 0) return 0;
    
#if defined(__GNUC__) || defined(__clang__)
    // Use compiler intrinsics for optimal performance
    if constexpr (sizeof(size_t) == 4) {
        return size_t(1) << (31 - __builtin_clz(static_cast<unsigned int>(x)));
    } else if constexpr (sizeof(size_t) == 8) {
        return size_t(1) << (63 - __builtin_clzll(static_cast<unsigned long long>(x)));
    } else {
        static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8, "Unsupported size_t size");
    }
#else
    // Fallback: bit-twiddling for other compilers
    #if SIZE_MAX == UINT32_MAX
        x |= (x >> 1);
        x |= (x >> 2);
        x |= (x >> 4);
        x |= (x >> 8);
        x |= (x >> 16);
    #elif SIZE_MAX == UINT64_MAX
        x |= (x >> 1);
        x |= (x >> 2);
        x |= (x >> 4);
        x |= (x >> 8);
        x |= (x >> 16);
        x |= (x >> 32);
    #endif
    return x - (x >> 1);
#endif
}

// Next power of 2 utility (complement to floor2p2)
[[maybe_unused]] inline size_t ceil2p2(size_t x) {
    if (x <= 1) return 1;
    return floor2p2(x - 1) << 1;
}

// Check if a number is power of 2
[[maybe_unused]] inline bool is_power_of_2(size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

// Simple error to string conversion (no dependencies)
inline const char* error_to_cstring(Error error) {
    return to_str(error);
}

} // namespace cler