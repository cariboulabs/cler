#pragma once

#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace cler {

[[noreturn]] inline void panic(const char* msg) {
    std::fprintf(stderr, "cler panic: %s\n", msg);
    std::abort();
}

// Stream output operator for Error enum (requires std::ostream)
inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}

template<typename... BlockRunners>
void print_flowgraph_execution_report(const DesktopFlowGraph<BlockRunners...>& fg) {
    if (!fg.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!fg.is_stopped()) {
            printf("FlowGraph is still running, can't print report.\n");
            return;
        }
    }

    printf("\n=== Execution Report ===\n");
    printf("FlowGraphConfig:\n");
    
    // Show scheduler type
    const char* scheduler_name = "Unknown";
    switch (fg.config().scheduler) {
        case SchedulerType::ThreadPerBlock: scheduler_name = "ThreadPerBlock"; break;
        case SchedulerType::FixedThreadPool: scheduler_name = "FixedThreadPool"; break;
        case SchedulerType::PinnedIslands: scheduler_name = "PinnedIslands"; break;
    }
    printf("  - Scheduler: %s\n", scheduler_name);
    
    if (fg.config().scheduler != SchedulerType::ThreadPerBlock) {
        printf("  - Workers: %zu\n", fg.config().num_workers);
        
    }
    
    printf("  - Detailed Stats: %s\n", fg.config().collect_detailed_stats ? "ENABLED" : "DISABLED");
    printf("\n");


    if (fg.config().collect_detailed_stats) {
        // Full detailed stats table
        printf("%-30s | %8s | %10s | %12s\n",
            "Block", "Success%", "CPU %", "AvgTime(us)");
        printf("%s\n", std::string(95, '-').c_str());

        for (const auto& s : fg.stats()) {
            size_t total = s.successful_procedures + s.failed_procedures;
            float success_rate = total > 0 ? (static_cast<float>(s.successful_procedures) / total) * 100.0f : 0.0f;
            
            printf("%-30s | %8.1f | %10.1f | %12.1f\n",
                s.name.c_str(),
                success_rate,
                s.get_cpu_utilization_percent(),
                s.get_avg_execution_time_us()
            );
        }
        
        printf("\nLegend:\n");
        printf("  Success%%   - Percentage of successful procedure calls\n");
        printf("  CPU %%      - CPU utilization: (runtime - dead_time) / runtime * 100\n");
        printf("  AvgTime(us) - Total thread lifetime divided by successful procedures (includes all overhead)\n");
    } else {
        // Ultra-performance mode - minimal stats
        printf("Performance Mode: Detailed stats collection disabled for maximum throughput.\n");
        printf("No runtime statistics are being collected.\n");
        printf("Block count: %zu\n", fg.stats().size());
    }
}

} // namespace cler