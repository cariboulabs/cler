#pragma once

#include "cler.hpp"
#include "cler_general_utils.hpp"
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
    printf("  - Adaptive Sleep: %s\n", fg.config().adaptive_sleep ? "ENABLED" : "DISABLED");
    if (fg.config().adaptive_sleep) {
        printf("      * Sleep Factor : %.2f\n", fg.config().adaptive_sleep_ramp_up_factor);
        printf("      * Max Sleep (us): %.2f\n", fg.config().adaptive_sleep_max_us);
        printf("      * Gain : %.2f\n", fg.config().adaptive_sleep_target_gain);
        printf("      * Decay Factor : %.2f\n", fg.config().adaptive_sleep_decay_factor);
        printf("      * Consecutive Fail Threshold: %zu\n", fg.config().adaptive_sleep_consecutive_fail_threshold);
    }
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

        printf("%-25s | %10.2f | %12.6f | %15.2f | %12.2f | %20.2f \n",
            s.name.c_str(),
            success_rate,
            s.avg_dead_time_us,
            s.total_dead_time_s,
            dead_ratio,
            s.final_adaptive_sleep_us
        );
    }

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