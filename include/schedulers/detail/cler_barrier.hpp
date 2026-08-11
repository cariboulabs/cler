#pragma once

#include "../cler_scheduler_config.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cler {
    namespace sched {
    namespace detail {

        template<typename TaskPolicy>
        class RepartitionBarrier {
        public:
            static constexpr uint32_t kNoRequest = (std::numeric_limits<uint32_t>::max)();

            void reset() {
                _partition_epoch.store(0, std::memory_order_relaxed);
                _request.store(kNoRequest, std::memory_order_relaxed);
                _barrier.store(barrier_word(0, 0), std::memory_order_relaxed);
                _count.store(0, std::memory_order_relaxed);
            }

            size_t count() const { return _count.load(std::memory_order_acquire); }

            uint32_t current_epoch() const { return _partition_epoch.load(std::memory_order_relaxed); }

            uint32_t requested_generation() const { return _request.load(std::memory_order_relaxed); }

            bool has_request() const { return requested_generation() != kNoRequest; }

            void request_current_epoch() {
                _request.store(_partition_epoch.load(std::memory_order_relaxed),
                               std::memory_order_release);
            }

            void bump_epoch_and_unpark() {
                _partition_epoch.fetch_add(1, std::memory_order_release);
                TaskPolicy::unpark(_partition_epoch);
            }

            template<typename StopRequested, typename WakeOthers, typename Repartition>
            void arrive(bool is_leader, uint32_t generation, size_t worker_count,
                        StopRequested stop_requested, WakeOthers wake_others,
                        Repartition repartition) {
                if (!arrive_at_barrier(generation)) return;

                if (!is_leader) {
                    while (_partition_epoch.load(std::memory_order_acquire) == generation &&
                           !stop_requested()) {
                        TaskPolicy::park(_partition_epoch, generation);
                    }
                    return;
                }

                while (barrier_arrived(_barrier.load(std::memory_order_acquire)) < worker_count &&
                       !stop_requested()) {
                    wake_others();
                    TaskPolicy::yield();
                }

                if (!stop_requested()) {
                    repartition();
                    _count.fetch_add(1, std::memory_order_relaxed);
                }

                _barrier.store(barrier_word(generation + 1, 0), std::memory_order_release);
                _request.store(kNoRequest, std::memory_order_release);
                _partition_epoch.fetch_add(1, std::memory_order_release);
                TaskPolicy::unpark(_partition_epoch);
            }

        private:
            static constexpr uint64_t barrier_word(uint32_t generation, uint32_t arrived) {
                return (static_cast<uint64_t>(generation) << 32) | arrived;
            }
            static constexpr uint32_t barrier_generation(uint64_t word) {
                return static_cast<uint32_t>(word >> 32);
            }
            static constexpr uint32_t barrier_arrived(uint64_t word) {
                return static_cast<uint32_t>(word);
            }

            bool arrive_at_barrier(uint32_t generation) {
                uint64_t word = _barrier.load(std::memory_order_acquire);
                while (barrier_generation(word) == generation) {
                    if (_barrier.compare_exchange_weak(word, word + 1,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) {
                        return true;
                    }
                }
                return false;
            }

            std::atomic<uint32_t> _partition_epoch{0};
            std::atomic<uint32_t> _request{kNoRequest};
            std::atomic<uint64_t> _barrier{0};
            std::atomic<size_t> _count{0};
        };

    }
    }
}
