#pragma once

#include <cstddef>

namespace cler {

    struct FlowGraphConfig;

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
