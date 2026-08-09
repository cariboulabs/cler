#pragma once

#include "cler_scheduler_config.hpp"
#include <cstddef>

namespace cler {
    namespace sched {

        template<typename Host>
        struct ThreadPerBlockScheduler {
            struct State {};

            static void start(Host host, State&, const FlowGraphConfig& config) {
                host.prepare_run();
                host.launch_all_blocks(config);
            }

            static void notify_stop(Host, State&) {}
        };

    }
}
