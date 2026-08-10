#pragma once

#include "cler_scheduler_config.hpp"
#include "detail/cler_partition.hpp"
#include "detail/cler_park.hpp"
#include "detail/cler_barrier.hpp"
#include "../task_policies/cler_task_policy_base.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cler {
    namespace sched {

        template<typename Host>
        struct PinnedIslandsScheduler {
            using TaskPolicy = typename Host::TaskPolicyType;
            using Partition = typename Host::PartitionType;
            using ParkGroup = sched::detail::ParkGroup<TaskPolicy, DEFAULT_MAX_WORKERS>;
            using WorkerParkState = typename ParkGroup::WorkerParkState;
            using RepartitionBarrier = sched::detail::RepartitionBarrier<TaskPolicy>;

            static constexpr size_t CALIBRATION_DEADLINE_CHECK_PASSES = 256;
            static constexpr size_t kLeaderWorker = 0;

            struct State {
                detail::WorkerQueueScheduler<Host::block_count, DEFAULT_MAX_WORKERS> queues;
                ParkGroup park_states;
                Partition partition;
                RepartitionBarrier barrier;
                std::chrono::steady_clock::time_point calibration_deadline;
                std::chrono::steady_clock::time_point next_repartition_check;
                size_t pinned_worker_count = 0;

                void reset() {
                    park_states.reset();
                    partition = Partition{};
                    barrier.reset();
                    pinned_worker_count = 0;
                }
            };

            struct WakePinnedWorkers {
                State* state;
                void operator()() const {
                    state->park_states.wake_others(detail::kNoWorker, state->pinned_worker_count);
                }
            };

            static void start(Host host, State& state, const FlowGraphConfig& config) {
                host.reset_stop_flag();
                host.prepare_run();

                if (config.collect_detailed_stats) {
                    host.mark_block_start_times();
                }

                host.warn_unresolved_edges();

                std::array<uint8_t, Host::block_count> regular_ids{};
                const size_t regular_count = host.collect_regular_blocks(regular_ids);

                state.calibration_deadline = std::chrono::steady_clock::now() +
                                             std::chrono::milliseconds(config.pinned_islands.calibration_ms);
                state.next_repartition_check = state.calibration_deadline;

                const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
                state.pinned_worker_count = regular_count == 0
                    ? 0
                    : (std::max)(size_t{1}, (std::min)(config.num_workers, max_worker_count));

                host.launch_may_block_tasks_sampled(config, WakePinnedWorkers{&state});

                if (regular_count == 0) return;

                host.mark_item_counters();
                host.rebuild_partition(state.pinned_worker_count, false, state.partition);
                state.queues.initialize_islands(state.partition.block_ids.data(),
                                                state.partition.island_begin.data(),
                                                state.partition.island_count);

                State* state_ptr = &state;
                for (size_t worker_id = 0; worker_id < state.pinned_worker_count; ++worker_id) {
                    host.add_task([host, state_ptr, worker_id, config]() {
                        worker_loop(host, *state_ptr, worker_id, config);
                    });
                }
            }

            static void notify_stop(Host, State& state) {
                state.park_states.wake_all();
                state.barrier.bump_epoch_and_unpark();
            }

        private:
            static void worker_loop(Host host, State& state, size_t worker_id, const FlowGraphConfig& config) {
                TaskPolicy::configure_thread_for_low_latency_sleep();
                if (!TaskPolicy::pin_to_core(config.pinned_islands.cpu_id_offset + worker_id)) {
                    host.note_affinity_failure();
                }

                WorkerParkState& park_state = state.park_states[worker_id];
                BackoffState backoff_state{};
                size_t zero_progress_passes = 0;
                size_t passes_since_deadline_check = 0;
                bool park_armed = false;
                bool pass_follows_park = false;
                uint32_t observed_sleep_epoch = 0;

                while (!host.stop_requested()) {
                    state.queues.reset_pass(worker_id);
                    bool did_work_in_pass = false;
                    size_t block_idx;

                    while (state.queues.get_next_block(worker_id, block_idx)) {
                        if (host.stop_requested()) break;

                        auto t_before = config.collect_detailed_stats
                            ? std::chrono::high_resolution_clock::now()
                            : std::chrono::high_resolution_clock::time_point{};

                        bool block_did_work = host.execute_block_sampled(block_idx, config);

                        if (!block_did_work && config.collect_detailed_stats) {
                            auto t_after = std::chrono::high_resolution_clock::now();
                            std::chrono::duration<double> dt = t_after - t_before;
                            host.add_block_dead_time(block_idx, dt.count());
                        }

                        did_work_in_pass = did_work_in_pass || block_did_work;
                    }

                    const bool deadline_check_due =
                        ++passes_since_deadline_check >= CALIBRATION_DEADLINE_CHECK_PASSES || pass_follows_park;
                    pass_follows_park = false;

                    if (worker_id == kLeaderWorker && deadline_check_due && !state.barrier.has_request()) {
                        passes_since_deadline_check = 0;
                        if (leader_should_repartition(host, state, config)) {
                            state.barrier.request_current_epoch();
                            state.park_states.wake_others(worker_id, state.pinned_worker_count);
                        }
                    }

                    const uint32_t requested_generation = state.barrier.requested_generation();
                    if (requested_generation != RepartitionBarrier::kNoRequest) {
                        if (park_armed) {
                            park_armed = false;
                            park_state.parked.store(false, std::memory_order_relaxed);
                        }
                        repartition_barrier(host, state, worker_id, requested_generation, config);
                        zero_progress_passes = 0;
                        TaskPolicy::backoff_reset(backoff_state);
                        continue;
                    }

                    if (did_work_in_pass) {
                        zero_progress_passes = 0;
                        park_armed = false;
                        if (park_state.parked.load(std::memory_order_relaxed)) {
                            park_state.parked.store(false, std::memory_order_relaxed);
                        }
                        TaskPolicy::backoff_reset(backoff_state);
                        state.park_states.wake_others(worker_id, state.pinned_worker_count);
                        continue;
                    }

                    ++zero_progress_passes;

                    if (zero_progress_passes <= config.pinned_islands.park_after_zero_passes) {
                        TaskPolicy::backoff(backoff_state);
                    } else if (!park_armed) {
                        park_armed = true;
                        observed_sleep_epoch = park_state.sleep_epoch.load(std::memory_order_acquire);
                        park_state.parked.store(true, std::memory_order_release);
                    } else {
                        park_state.park_events.fetch_add(1, std::memory_order_relaxed);
                        TaskPolicy::park(park_state.sleep_epoch, observed_sleep_epoch);
                        park_armed = false;
                        pass_follows_park = true;
                        park_state.parked.store(false, std::memory_order_relaxed);
                    }
                }

                park_state.parked.store(false, std::memory_order_relaxed);

                if (config.collect_detailed_stats) {
                    for (size_t i = 0; i < Host::block_count; ++i) {
                        if (state.queues.is_block_owner(worker_id, i)) {
                            host.set_block_runtime_from_start(i);
                        }
                    }
                }
            }

            static bool leader_should_repartition(Host host, State& state, const FlowGraphConfig& config) {
                if (config.pinned_islands.manual_islands != nullptr) return false;

                const bool calibrated = state.barrier.count() != 0;
                if (calibrated && config.pinned_islands.repartition_check_ms == 0) return false;

                const auto now = std::chrono::steady_clock::now();
                if (!calibrated) return now >= state.calibration_deadline;
                if (now < state.next_repartition_check) return false;
                state.next_repartition_check = now + std::chrono::milliseconds(config.pinned_islands.repartition_check_ms);

                std::array<double, Host::block_count> weights{};
                host.collect_block_weights(state.partition, weights);

                Partition candidate;
                host.rebuild_partition(state.pinned_worker_count, true, candidate);

                return sched::detail::max_island_weight(candidate, weights) <
                       sched::detail::REPARTITION_IMPROVEMENT_RATIO * sched::detail::max_island_weight(state.partition, weights);
            }

            static void repartition_barrier(Host host, State& state, size_t worker_id, uint32_t generation,
                                            const FlowGraphConfig& config) {
                state.barrier.arrive(
                    worker_id == kLeaderWorker, generation, state.pinned_worker_count,
                    [host]() { return host.stop_requested(); },
                    [&state, worker_id]() { state.park_states.wake_others(worker_id, state.pinned_worker_count); },
                    [host, &state, &config]() mutable {
                        host.rebuild_partition(state.pinned_worker_count, true, state.partition);
                        host.mark_item_counters();
                        state.queues.initialize_islands(state.partition.block_ids.data(),
                                                state.partition.island_begin.data(),
                                                state.partition.island_count);
                        state.next_repartition_check = std::chrono::steady_clock::now() +
                                                       std::chrono::milliseconds(config.pinned_islands.repartition_check_ms);
                    });
            }
        };

    }
}
