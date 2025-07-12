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

template<typename... BlockRunners>
void print_flowgraph_execution_report(const FlowGraph<BlockRunners...>& fg) {
    if (!fg.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!fg.is_stopped()) {
            printf("FlowGraph is still running, can't print report.\n");
            return;
        }
    }

    printf("\n=== Execution Report ===\n");
    printf("FlowGraphConfig:\n");
    printf("  - Adaptive Sleep: %s\n", fg.config().adaptive_sleep ? "ENABLED" : "DISABLED");
    printf("\n");

    // Table header
    printf("%-25s | %10s | %12s | %15s | %12s | %20s | %22s\n",
        "Block", "Success %", "Avg Dead (us)", "Total Dead (s)",
        "Dead Ratio %", "Adaptive Sleep (us)", "Final Consecutive Fails");
    printf("%s\n", std::string(125, '-').c_str());

    for (const auto& s : fg.stats()) {
        size_t total = s.successful_procedures + s.failed_procedures;
        float success_rate = total > 0 ? (static_cast<float>(s.successful_procedures) / total) * 100.0f : 0.0f;
        float dead_ratio = s.total_runtime_s > 0 ? (s.total_dead_time_s / s.total_runtime_s) * 100.0f : 0.0f;

        printf("%-25s | %10.2f | %12.6f | %15.2f | %12.2f | %20.2f | %22zu\n",
            s.name.c_str(),
            success_rate,
            s.avg_dead_time_us,
            s.total_dead_time_s,
            dead_ratio,
            s.final_adaptive_sleep_us,
            s.final_consecutive_fails
        );
    }

    printf("\nExecution Report Guidance:\n");
    printf("  - Blocks with a HIGH dead ratio or low success rate MAY be waiting on data.\n");
    printf("  - Blocks with consistently HIGH success rate MAY be the throughput bottlenecks.\n");
    printf("  - Adaptive sleep helps reduce CPU spin during dead time. Tune or disable if needed by configuration.\n");
    printf("  - The Adaptive sleep is based on Dead Ratio to spot blocks wasting time waiting for data.\n");
    printf("  - Final Consecutive Fails shows if a block was stalling at shutdown.\n");
}

}