#pragma once

// Desktop-specific utilities for CLER framework
// These require full desktop environment (std::iostream, printf, std::thread)

#include "cler.hpp"
#include "cler_general_addons.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <cstdio>

namespace cler {

// Stream output operator for Error enum (requires std::ostream)
inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}

// Comprehensive flowgraph execution report with formatted console output
template<typename ThreadingPolicy, typename... BlockRunners>
void print_flowgraph_execution_report(const FlowGraph<ThreadingPolicy, BlockRunners...>& fg) {
    // Wait briefly if flowgraph is still running
    if (!fg.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!fg.is_stopped()) {
            printf("FlowGraph is still running, can't print report.\n");
            return;
        }
    }

    printf("\n=== CLER FlowGraph Execution Report ===\n");
    
    // Configuration section
    printf("FlowGraphConfig:\n");
    printf("  - Adaptive Sleep: %s\n", fg.config().adaptive_sleep ? "ENABLED" : "DISABLED");
    if (fg.config().adaptive_sleep) {
        printf("      * Ramp Up Factor: %.2f\n", fg.config().adaptive_sleep_ramp_up_factor);
        printf("      * Max Sleep (μs): %.2f\n", fg.config().adaptive_sleep_max_us);
        printf("      * Target Gain: %.2f\n", fg.config().adaptive_sleep_target_gain);
        printf("      * Decay Factor: %.2f\n", fg.config().adaptive_sleep_decay_factor);
        printf("      * Fail Threshold: %zu\n", fg.config().adaptive_sleep_consecutive_fail_threshold);
    }
    printf("\n");

    // Table header
    printf("%-25s | %10s | %12s | %15s | %12s | %20s\n",
        "Block Name", "Success %", "Avg Dead (μs)", "Total Dead (s)",
        "Dead Ratio %", "Final Sleep (μs)");
    printf("%s\n", std::string(110, '-').c_str());

    // Block statistics
    const auto& stats = fg.stats();
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& s = stats[i];
        auto basic_stats = compute_block_stats(fg, i);

        printf("%-25s | %10.2f | %12.6f | %15.2f | %12.2f | %20.2f\n",
            s.name.c_str(),
            basic_stats.success_rate_percent,
            s.avg_dead_time_us,
            s.total_dead_time_s,
            basic_stats.dead_time_ratio_percent,
            s.final_adaptive_sleep_us
        );
    }

    // Guidance section
    printf("\n=== Performance Guidance ===\n");
    printf("• Success %% shows how often blocks completed useful work\n");
    printf("• Dead Ratio indicates time spent waiting for data/space\n");
    printf("• HIGH Dead Ratio blocks are often blocked by upstream bottlenecks\n");
    printf("• Consistently HIGH Success %% blocks may be throughput bottlenecks\n");
    printf("• Consider buffer sizing and block processing chunk sizes\n");
    printf("\n");

    if (fg.config().adaptive_sleep) {
        printf("=== Adaptive Sleep Analysis ===\n");
        printf("Adaptive sleep reduces CPU usage by sleeping during repeated failures.\n");
        printf("Sleep time = max(previous_sleep × ramp_up + 1, avg_dead_time × target_gain)\n");
        printf("\nTuning recommendations:\n");
        printf("• Disable for maximum responsiveness (higher CPU usage)\n");
        printf("• Increase Ramp Up/Target Gain for more aggressive sleeping\n");
        printf("• Decrease Max Sleep if recovery feels too slow\n");
        printf("• Lower Decay Factor for faster sleep reduction after recovery\n");
        printf("• Adjust Fail Threshold based on data pattern (burst vs steady)\n");
        printf("\n");
    }

    // Performance summary
    size_t total_success = 0, total_procedures = 0;
    double total_runtime = 0;
    for (const auto& s : stats) {
        total_success += s.successful_procedures;
        total_procedures += s.successful_procedures + s.failed_procedures;
        total_runtime = std::max(total_runtime, s.total_runtime_s);
    }

    if (total_procedures > 0) {
        printf("=== Overall Performance ===\n");
        printf("Total Runtime: %.2f seconds\n", total_runtime);
        printf("Overall Success Rate: %.2f%%\n", 
               (static_cast<double>(total_success) / total_procedures) * 100.0);
        if (total_runtime > 0) {
            printf("Average Throughput: %.0f procedures/second\n", 
                   total_procedures / total_runtime);
        }
        printf("\n");
    }
}

// Simplified single-line status report
template<typename ThreadingPolicy, typename... BlockRunners>
void print_flowgraph_status(const FlowGraph<ThreadingPolicy, BlockRunners...>& fg) {
    const auto& stats = fg.stats();
    printf("CLER Status: ");
    
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& s = stats[i];
        auto basic_stats = compute_block_stats(fg, i);
        printf("%s(%.1f%%) ", s.name.c_str(), basic_stats.success_rate_percent);
    }
    
    printf("- %s\n", fg.is_stopped() ? "STOPPED" : "RUNNING");
}

// CSV export for further analysis
template<typename ThreadingPolicy, typename... BlockRunners>
void export_flowgraph_stats_csv(const FlowGraph<ThreadingPolicy, BlockRunners...>& fg, 
                                const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open %s for writing\n", filename);
        return;
    }

    // CSV header
    fprintf(file, "block_name,successful_procedures,failed_procedures,success_rate_percent,"
                  "avg_dead_time_us,total_dead_time_s,dead_ratio_percent,total_runtime_s,"
                  "final_adaptive_sleep_us\n");

    // CSV data
    const auto& stats = fg.stats();
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& s = stats[i];
        auto basic_stats = compute_block_stats(fg, i);

        fprintf(file, "%s,%zu,%zu,%.2f,%.6f,%.2f,%.2f,%.2f,%.2f\n",
            s.name.c_str(),
            s.successful_procedures,
            s.failed_procedures,
            basic_stats.success_rate_percent,
            s.avg_dead_time_us,
            s.total_dead_time_s,
            basic_stats.dead_time_ratio_percent,
            s.total_runtime_s,
            s.final_adaptive_sleep_us
        );
    }

    fclose(file);
    printf("FlowGraph statistics exported to %s\n", filename);
}

} // namespace cler