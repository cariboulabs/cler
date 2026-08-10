#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cler {

    enum class SchedulerType {
        ThreadPerBlock,
        FixedThreadPool,
        PinnedIslands
    };
    struct FixedThreadPoolConfig {
        bool pin_workers = false;
    };

    struct PinnedIslandsConfig {
        size_t calibration_ms = 500;
        size_t repartition_check_ms = 5000;
        size_t cpu_id_offset = 0;
        size_t park_after_zero_passes = 4;
        const char* const* manual_islands = nullptr;
        size_t manual_island_count = 0;
        bool report_partition = false;
    };

    struct FlowGraphConfig {
        SchedulerType scheduler = SchedulerType::ThreadPerBlock;
        size_t num_workers = 4;
        bool collect_detailed_stats = false;
        size_t max_calls_per_tick = 4;
        FixedThreadPoolConfig fixed_thread_pool{};
        PinnedIslandsConfig pinned_islands{};
    };


    #ifndef CLER_DEFAULT_MAX_WORKERS
    #define CLER_DEFAULT_MAX_WORKERS (8)
    #endif
    constexpr size_t DEFAULT_MAX_WORKERS = CLER_DEFAULT_MAX_WORKERS;

    struct Edge {
        uint8_t producer;
        uint8_t consumer;
    };

    namespace sched {

        template<size_t MaxBlocks, size_t MaxWorkers>
        struct Partition {
            std::array<uint8_t, MaxBlocks> block_ids{};
            std::array<uint16_t, MaxWorkers + 1> island_begin{};
            uint16_t block_count = 0;
            uint16_t island_count = 0;

            uint16_t island_size(size_t island) const {
                return static_cast<uint16_t>(island_begin[island + 1] - island_begin[island]);
            }

            size_t island_of(uint8_t block_id) const {
                for (size_t w = 0; w < island_count; ++w) {
                    for (uint16_t k = island_begin[w]; k < island_begin[w + 1]; ++k) {
                        if (block_ids[k] == block_id) return w;
                    }
                }
                return island_count;
            }
        };

    }
}
