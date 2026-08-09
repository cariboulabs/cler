#pragma once

#include <cstddef>

namespace cler {

    enum class SchedulerType {
        ThreadPerBlock,
        FixedThreadPool,
        PinnedIslands
    };
    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 4;
        bool collect_detailed_stats = false;
        size_t max_calls_per_tick = 4;
        bool pin_workers = false;
        size_t calibration_ms = 500;
        size_t repartition_check_ms = 5000;
        size_t cpu_id_offset = 0;
        size_t park_after_zero_passes = 4;
    };

}
