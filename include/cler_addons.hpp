#pragma once

//this file includes things that might be optional for the user to include
//examples: io, hardware specific, logging, etc.

#include "cler.hpp"
#include <iostream>

namespace cler {
inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}


[[maybe_unused]] inline size_t floor2p2(size_t x) {
if (x == 0) return 0;
#if defined(__GNUC__) || defined(__clang__)
    if constexpr (sizeof(size_t) == 4) {
        return size_t(1) << (31 - __builtin_clz(static_cast<unsigned int>(x)));
    } else if constexpr (sizeof(size_t) == 8) {
        return size_t(1) << (63 - __builtin_clzll(static_cast<unsigned long long>(x)));
    } else
        static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8, "Unsupported size_t size");
#else
    // Fallback: bit-twiddling
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

}