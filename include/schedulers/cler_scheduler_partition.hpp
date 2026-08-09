#pragma once

#include "cler_scheduler_topology.hpp"
#include "../cler_platform.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cler {
    namespace sched {

        static constexpr size_t COST_SAMPLE_PERIOD_CALLS = 61;
        static constexpr double CROSS_EDGE_PENALTY_NS = 200.0;
        static constexpr double REPARTITION_IMPROVEMENT_RATIO = 0.8;

        inline uint64_t double_to_bits(double value) {
            uint64_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        inline double bits_to_double(uint64_t bits) {
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        struct alignas(platform::cache_line_size) SchedulerCostSample {
            size_t calls_until_sample = COST_SAMPLE_PERIOD_CALLS;
            std::atomic<uint64_t> ewma_ns_per_call_bits{0};
            std::atomic<uint64_t> ewma_items_per_call_bits{0};
        };

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

        template<size_t MaxBlocks>
        void collect_block_weights(const std::array<SchedulerCostSample, MaxBlocks>& samples,
                                   const std::array<uint8_t, MaxBlocks>& order, size_t count,
                                   std::array<double, MaxBlocks>& weights) {
            std::array<double, MaxBlocks> sorted{};
            size_t sampled = 0;

            for (size_t i = 0; i < count; ++i) {
                const uint8_t id = order[i];
                const auto& sample = samples[id];
                const double ns = bits_to_double(sample.ewma_ns_per_call_bits.load(std::memory_order_relaxed));
                const double items = bits_to_double(sample.ewma_items_per_call_bits.load(std::memory_order_relaxed));
                weights[id] = ns > 0.0 ? ns / (std::max)(items, 1.0) : 0.0;
                if (weights[id] > 0.0) sorted[sampled++] = weights[id];
            }

            double median = 1.0;
            if (sampled > 0) {
                std::sort(sorted.begin(), sorted.begin() + sampled);
                median = sorted[sampled / 2];
            }
            for (size_t i = 0; i < count; ++i) {
                if (weights[order[i]] <= 0.0) weights[order[i]] = median;
            }
        }

        template<size_t MaxBlocks, size_t MaxWorkers>
        double max_island_weight(const Partition<MaxBlocks, MaxWorkers>& partition,
                                 const std::array<double, MaxBlocks>& weights) {
            double worst = 0.0;
            for (size_t w = 0; w < partition.island_count; ++w) {
                double island = 0.0;
                for (uint16_t k = partition.island_begin[w]; k < partition.island_begin[w + 1]; ++k) {
                    island += weights[partition.block_ids[k]];
                }
                worst = (std::max)(worst, island);
            }
            return worst;
        }

        template<size_t MaxBlocks, size_t MaxWorkers>
        void build_partition(const Edge* edges, size_t edge_count,
                             const std::array<SchedulerCostSample, MaxBlocks>& samples,
                             const std::array<uint8_t, MaxBlocks>& regular_ids, size_t regular_count,
                             size_t worker_count, bool use_costs, bool topology_is_complete,
                             Partition<MaxBlocks, MaxWorkers>& out) {
            std::array<uint8_t, MaxBlocks> order{};
            if (topology_is_complete) {
                topo_sort_blocks<MaxBlocks>(edges, edge_count, regular_ids.data(), regular_count, order);
            } else {
                order = regular_ids;
            }

            const size_t islands = (std::max)(size_t{1}, (std::min)(worker_count, regular_count));
            out.block_ids = order;
            out.block_count = static_cast<uint16_t>(regular_count);
            out.island_count = static_cast<uint16_t>(islands);

            if (!use_costs || !topology_is_complete) {
                size_t cursor = 0;
                for (size_t w = 0; w < islands; ++w) {
                    out.island_begin[w] = static_cast<uint16_t>(cursor);
                    cursor += regular_count / islands + (w < regular_count % islands ? 1 : 0);
                }
                out.island_begin[islands] = static_cast<uint16_t>(regular_count);
                return;
            }

            std::array<double, MaxBlocks> weights{};
            collect_block_weights<MaxBlocks>(samples, order, regular_count, weights);

            std::array<double, MaxBlocks + 1> prefix{};
            for (size_t i = 0; i < regular_count; ++i) prefix[i + 1] = prefix[i] + weights[order[i]];

            std::array<uint16_t, MaxBlocks> crossings{};
            count_cut_crossings<MaxBlocks>(edges, edge_count, order, regular_count, crossings);

            std::array<double, MaxBlocks + 1> prev_max{};
            std::array<double, MaxBlocks + 1> prev_crossings{};
            std::array<double, MaxBlocks + 1> island_max{};
            std::array<double, MaxBlocks + 1> total_crossings{};
            std::array<std::array<uint16_t, MaxBlocks + 1>, MaxWorkers + 1> choice{};

            const double infinity = (std::numeric_limits<double>::max)();
            for (size_t end = 0; end <= regular_count; ++end) {
                prev_max[end] = (end == 0) ? 0.0 : infinity;
                prev_crossings[end] = 0.0;
            }

            for (size_t k = 1; k <= islands; ++k) {
                for (size_t end = 0; end <= regular_count; ++end) island_max[end] = infinity;
                for (size_t end = k; end <= regular_count - (islands - k); ++end) {
                    double best_objective = infinity;
                    for (size_t start = k - 1; start < end; ++start) {
                        if (prev_max[start] == infinity) continue;
                        const double candidate_max = (std::max)(prev_max[start], prefix[end] - prefix[start]);
                        const double candidate_crossings = prev_crossings[start] + (start > 0 ? crossings[start] : 0);
                        const double objective = candidate_max + CROSS_EDGE_PENALTY_NS * candidate_crossings;
                        if (objective < best_objective) {
                            best_objective = objective;
                            island_max[end] = candidate_max;
                            total_crossings[end] = candidate_crossings;
                            choice[k][end] = static_cast<uint16_t>(start);
                        }
                    }
                }
                prev_max = island_max;
                prev_crossings = total_crossings;
            }

            size_t end = regular_count;
            out.island_begin[islands] = static_cast<uint16_t>(regular_count);
            for (size_t k = islands; k >= 1; --k) {
                const uint16_t start = choice[k][end];
                out.island_begin[k - 1] = start;
                end = start;
            }
        }

    }
}
