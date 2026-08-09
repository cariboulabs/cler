#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cler {

    struct Edge {
        uint8_t producer;
        uint8_t consumer;
    };

    namespace sched {

        template<size_t MaxBlocks>
        void topo_sort_blocks(const Edge* edges, size_t edge_count,
                              const uint8_t* ids, size_t count,
                              std::array<uint8_t, MaxBlocks>& out) {
            std::array<uint16_t, MaxBlocks> indegree{};
            std::array<uint8_t, MaxBlocks> in_set{};
            std::array<uint8_t, MaxBlocks> emitted{};

            for (size_t i = 0; i < count; ++i) in_set[ids[i]] = 1;
            for (size_t e = 0; e < edge_count; ++e) {
                const Edge& edge = edges[e];
                if (edge.producer == edge.consumer) continue;
                if (in_set[edge.producer] && in_set[edge.consumer]) ++indegree[edge.consumer];
            }

            size_t emitted_count = 0;
            bool progressed = true;
            while (progressed) {
                progressed = false;
                for (size_t i = 0; i < count; ++i) {
                    const uint8_t b = ids[i];
                    if (emitted[b] || indegree[b] != 0) continue;
                    emitted[b] = 1;
                    out[emitted_count++] = b;
                    progressed = true;
                    for (size_t e = 0; e < edge_count; ++e) {
                        const Edge& edge = edges[e];
                        if (edge.producer != b || edge.producer == edge.consumer) continue;
                        if (in_set[edge.consumer] && !emitted[edge.consumer]) --indegree[edge.consumer];
                    }
                }
            }
            for (size_t i = 0; i < count; ++i) {
                if (!emitted[ids[i]]) out[emitted_count++] = ids[i];
            }
        }

        template<size_t MaxBlocks>
        void count_cut_crossings(const Edge* edges, size_t edge_count,
                                 const std::array<uint8_t, MaxBlocks>& order, size_t count,
                                 std::array<uint16_t, MaxBlocks>& crossings) {
            std::array<uint16_t, MaxBlocks> position{};
            std::array<uint8_t, MaxBlocks> in_order{};
            for (size_t i = 0; i < count; ++i) {
                position[order[i]] = static_cast<uint16_t>(i);
                in_order[order[i]] = 1;
            }

            for (size_t e = 0; e < edge_count; ++e) {
                const Edge& edge = edges[e];
                if (!in_order[edge.producer] || !in_order[edge.consumer]) continue;
                const uint16_t producer_pos = position[edge.producer];
                const uint16_t consumer_pos = position[edge.consumer];
                const uint16_t low = (std::min)(producer_pos, consumer_pos);
                const uint16_t high = (std::max)(producer_pos, consumer_pos);
                for (uint16_t cut = static_cast<uint16_t>(low + 1); cut <= high; ++cut) {
                    ++crossings[cut];
                }
            }
        }

    }
}
