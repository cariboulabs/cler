#pragma once

#include "../cler_scheduler_config.hpp"
#include "../../cler_platform.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cler {
    namespace sched {
    namespace detail {

        static constexpr size_t kNoWorker = (std::numeric_limits<size_t>::max)();

        template<typename TaskPolicy, size_t MaxWorkers>
        class ParkGroup {
        public:
            struct alignas(platform::cache_line_size) WorkerParkState {
                std::atomic<uint32_t> sleep_epoch{0};
                std::atomic<bool> parked{false};
                std::atomic<uint64_t> park_events{0};
            };

            WorkerParkState& operator[](size_t worker_id) { return _states[worker_id]; }
            const WorkerParkState& operator[](size_t worker_id) const { return _states[worker_id]; }

            void reset() {
                for (auto& state : _states) {
                    state.sleep_epoch.store(0, std::memory_order_relaxed);
                    state.parked.store(false, std::memory_order_relaxed);
                    state.park_events.store(0, std::memory_order_relaxed);
                }
            }

            void wake_others(size_t self_id, size_t worker_count) {
                for (size_t w = 0; w < worker_count; ++w) {
                    if (w == self_id) continue;
                    WorkerParkState& state = _states[w];
                    if (!state.parked.load(std::memory_order_relaxed)) continue;
                    if (!state.parked.exchange(false, std::memory_order_acq_rel)) continue;
                    state.sleep_epoch.fetch_add(1, std::memory_order_release);
                    TaskPolicy::unpark(state.sleep_epoch);
                }
            }

            void wake_all() {
                for (auto& state : _states) {
                    state.parked.store(false, std::memory_order_relaxed);
                    state.sleep_epoch.fetch_add(1, std::memory_order_release);
                    TaskPolicy::unpark(state.sleep_epoch);
                }
            }

            uint64_t total_park_events() const {
                uint64_t total = 0;
                for (const auto& state : _states) {
                    total += state.park_events.load(std::memory_order_relaxed);
                }
                return total;
            }

        private:
            std::array<WorkerParkState, MaxWorkers> _states;
        };

    }
    }
}
