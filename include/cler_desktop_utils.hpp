#pragma once

#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <cstdio>

namespace cler {

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
        case SchedulerType::AdaptiveLoadBalancing: scheduler_name = "AdaptiveLoadBalancing"; break;
    }
    printf("  - Scheduler: %s\n", scheduler_name);
    
    if (fg.config().scheduler != SchedulerType::ThreadPerBlock) {
        printf("  - Workers: %zu\n", fg.config().num_workers);
        
        if (fg.config().scheduler == SchedulerType::AdaptiveLoadBalancing) {
            printf("  - Load Balancing: %s\n", fg.config().load_balancing ? "ENABLED" : "DISABLED");
            if (fg.config().load_balancing) {
                printf("      * Interval: %zu procedure calls\n", fg.config().load_balancing_interval);
                printf("      * Threshold: %.1f%%\n", fg.config().load_balancing_threshold * 100.0);
            }
        }
    }
    
    printf("  - Adaptive Sleep: %s\n", fg.config().adaptive_sleep ? "ENABLED" : "DISABLED");
    if (fg.config().adaptive_sleep) {
        printf("      * Multiplier: %.2f\n", fg.config().adaptive_sleep_multiplier);
        printf("      * Max Sleep (us): %.1f\n", fg.config().adaptive_sleep_max_us);
        printf("      * Fail Threshold: %zu\n", fg.config().adaptive_sleep_fail_threshold);
    }
    printf("\n");


    // Optimized table header - removed worker reassignments (load balancing debug info)
    printf("%-20s | %8s | %10s | %12s | %10s | %12s | %8s\n",
        "Block", "Success%", "Samples", "Throughput", "CPU %", "AvgTime(us)", "Dead %");
    printf("%s\n", std::string(95, '-').c_str());

    for (const auto& s : fg.stats()) {
        size_t total = s.successful_procedures + s.failed_procedures;
        float success_rate = total > 0 ? (static_cast<float>(s.successful_procedures) / total) * 100.0f : 0.0f;
        float dead_ratio = s.total_runtime_s > 0 ? (s.total_dead_time_s / s.total_runtime_s) * 100.0f : 0.0f;
        
        // Use optimized getter methods (calculated offline, no runtime overhead)
        double throughput = s.get_throughput_samples_per_sec();

        printf("%-20s | %8.1f | %10zu | %12.1f | %10.1f | %12.2f | %8.1f\n",
            s.name.c_str(),
            success_rate,
            s.samples_processed,
            throughput / 1000.0,  // Display as KSamples/sec
            s.get_cpu_utilization_percent(),
            s.get_avg_execution_time_us(),
            dead_ratio
        );
    }
}

} // namespace cler