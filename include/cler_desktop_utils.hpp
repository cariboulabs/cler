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
            printf("  - Load Balancing: %s\n", fg.config().enable_load_balancing ? "ENABLED" : "DISABLED");
            if (fg.config().enable_load_balancing) {
                printf("      * Rebalance Interval: %zu procedure calls\n", fg.config().rebalance_interval);
                printf("      * Imbalance Threshold: %.1f%%\n", fg.config().load_balance_threshold * 100.0);
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


    // Enhanced table header with new statistics
    printf("%-20s | %8s | %10s | %12s | %10s | %12s | %15s | %8s\n",
        "Block", "Success%", "Samples", "Throughput", "CPU %", "AvgTime(us)", "Reassignments", "Dead %");
    printf("%s\n", std::string(110, '-').c_str());

    for (const auto& s : fg.stats()) {
        size_t total = s.successful_procedures + s.failed_procedures;
        float success_rate = total > 0 ? (static_cast<float>(s.successful_procedures) / total) * 100.0f : 0.0f;
        float dead_ratio = s.total_runtime_s > 0 ? (s.total_dead_time_s / s.total_runtime_s) * 100.0f : 0.0f;
        
        // Calculate throughput if we have samples and runtime
        double throughput = 0.0;
        if (s.total_runtime_s > 0 && s.samples_processed > 0) {
            throughput = s.samples_processed / s.total_runtime_s;
        }

        printf("%-20s | %8.1f | %10zu | %12.1f | %10.1f | %12.2f | %15zu | %8.1f\n",
            s.name.c_str(),
            success_rate,
            s.samples_processed,
            throughput / 1000.0,  // Display as KSamples/sec
            s.cpu_utilization_percent,
            s.avg_execution_time_us,
            s.worker_reassignments,
            dead_ratio
        );
    }
    
    printf("\nUnits: Throughput in KSamples/sec, Times in microseconds\n");

    printf("\n=== Guidance ===\n");
    printf("• Success %% shows how often the block's procedure completed useful work.\n");
    printf("• Dead Ratio indicates how much time was spent waiting for data.\n");
    printf("• Blocks with HIGH Dead Ratio or low Success %% are often blocked by upstream blocks.\n");
    printf("• Blocks with consistently HIGH Success %% can be throughput bottlenecks.\n");
    printf("\n");

    if (fg.config().adaptive_sleep) {
        printf("=== About Adaptive Sleep ===\n");
        printf("Adaptive sleep helps reduce CPU spin by sleeping when blocks repeatedly fail\n");
        printf("due to lack of data. It uses Dead Ratio and fail streaks to adjust sleep time.\n");
        printf("You can tune or disable it via FlowGraphConfig.\n");
        printf("\n");

        printf("=== Tuning Adaptive Sleep ===\n");
        printf("• Sleep time is computed as: max(previous sleep * Ramp Up + 1, avg_dead_time  * Target Gain)\n");
        printf("• Disable Adaptive Sleep for maximum responsiveness but higher CPU usage.\n");
        printf("• Increase Ramp Up Factor or Target Gain to sleep more when dead ratio is high.\n");
        printf("• Lower Max Sleep if blocks feel too slow to recover.\n");
        printf("• Lower Decay Factor if you want sleep to drop quickly after recovery.\n");
        printf("• Raise Consecutive Fail Threshold for bursty data, lower for steady streams.\n");
    }
}

} // namespace cler