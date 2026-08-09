#pragma once

#include "detail/cler_partition.hpp"
#include "../task_policies/cler_task_policy_base.hpp"
#include <algorithm>
#include <chrono>
#include "cler_scheduler_config.hpp"
#include <cstddef>

namespace cler {
    namespace sched {

        template<typename Host>
        struct FixedThreadPoolScheduler {
            using TaskPolicy = typename Host::TaskPolicyType;

            struct State {};

            static void start(Host host, State&, const FlowGraphConfig& config) {
                host.reset_stop_flag();
                host.prepare_run();

                if (config.collect_detailed_stats) {
                    host.mark_block_start_times();
                }

                const size_t regular_count = host.regular_block_count();

                host.launch_may_block_tasks(config);

                if (regular_count == 0) return;

                const size_t max_worker_count = (std::min)(DEFAULT_MAX_WORKERS, regular_count);
                const size_t requested_workers = (std::max)(size_t{2}, config.num_workers);
                const size_t effective_worker_count = (std::max)(size_t{1}, (std::min)(requested_workers, max_worker_count));

                if (effective_worker_count >= regular_count) {
                    host.launch_task_per_regular_block(config);
                } else {
                    host.initialize_worker_queues(effective_worker_count);

                    for (size_t worker_id = 0; worker_id < effective_worker_count; ++worker_id) {
                        host.add_task([host, worker_id, config]() {
                            worker_loop(host, worker_id, config);
                        });
                    }
                }
            }

            static void notify_stop(Host, State&) {}

        private:
            static void worker_loop(Host host, size_t worker_id, const FlowGraphConfig& config) {
                TaskPolicy::configure_thread_for_low_latency_sleep();
                if (config.pin_workers) {
                    TaskPolicy::pin_to_core(worker_id);
                }

                BackoffState backoff_state{};

                while (!host.stop_requested()) {
                    host.reset_worker_pass(worker_id);
                    bool did_work_in_pass = false;
                    size_t block_idx;

                    while (host.next_worker_block(worker_id, block_idx)) {
                        if (host.stop_requested()) break;

                        auto t_before = config.collect_detailed_stats
                            ? std::chrono::high_resolution_clock::now()
                            : std::chrono::high_resolution_clock::time_point{};

                        bool block_did_work = host.execute_block(block_idx, config);

                        if (!block_did_work && config.collect_detailed_stats) {
                            auto t_after = std::chrono::high_resolution_clock::now();
                            std::chrono::duration<double> dt = t_after - t_before;
                            host.add_block_dead_time(block_idx, dt.count());
                        }

                        did_work_in_pass = did_work_in_pass || block_did_work;
                    }

                    if (did_work_in_pass) {
                        TaskPolicy::backoff_reset(backoff_state);
                    } else {
                        TaskPolicy::backoff(backoff_state);
                    }
                }

                if (config.collect_detailed_stats) {
                    host.finalize_worker_stats(worker_id);
                }
            }
        };

    }
}
