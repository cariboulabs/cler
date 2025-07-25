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

// Factory methods for FlowGraphConfig 
// Common configurations for typical use cases
namespace flowgraph_config {

    // Conservative embedded configuration
    inline FlowGraphConfig embedded_optimized() {
        FlowGraphConfig config;
        config.scheduler = SchedulerType::FixedThreadPool;
        config.num_workers = 2;  // Conservative for embedded
        return config;
    }
    
    // Desktop performance configuration
    inline FlowGraphConfig desktop_performance() {
        FlowGraphConfig config;
        config.scheduler = SchedulerType::FixedThreadPool;
        config.num_workers = 4;  // Good default for most desktop systems
        return config;
    }
    
    // Adaptive load balancing configuration
    inline FlowGraphConfig adaptive_load_balancing() {
        FlowGraphConfig config;
        config.scheduler = SchedulerType::AdaptiveLoadBalancing;
        config.num_workers = 4;  // Good default for load balancing
        config.enable_load_balancing = true;
        config.rebalance_interval = 1000;
        config.load_balance_threshold = 0.2;
        return config;
    }
    
    // Thread-per-block with adaptive sleep (for low-rate data scenarios)
    inline FlowGraphConfig thread_per_block_adaptive_sleep() {
        FlowGraphConfig config;
        config.scheduler = SchedulerType::ThreadPerBlock;
        config.adaptive_sleep = true;
        config.adaptive_sleep_multiplier = 1.5;     // Default: moderate growth
        config.adaptive_sleep_max_us = 5000.0;      // Default: 5ms max sleep
        config.adaptive_sleep_fail_threshold = 10;  // Default: start sleeping after 10 fails
        return config;
    }

} // namespace flowgraph_config

} // namespace cler