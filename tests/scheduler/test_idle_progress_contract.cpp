#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/utils/throughput.hpp"
#include <thread>

namespace {

struct SilentSource : public cler::BlockBase {
    SilentSource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>*) {
        return cler::Error::NotEnoughSamples;
    }
};

constexpr size_t kBufferSize = 4096;
constexpr auto kIdleWindow = std::chrono::milliseconds(200);

}

TEST(IdleProgressContract, IdleSinkNullParksInsteadOfSpinning) {
    SilentSource source("SilentSource");
    SinkNullBlock<float> sink("SinkNull", nullptr, nullptr, kBufferSize);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(1);
    config.collect_detailed_stats = true;
    fg.run(config);
    std::this_thread::sleep_for(kIdleWindow);
    fg.stop();

    EXPECT_GT(fg.total_park_events(), 0u)
        << "idle graph never parked; a block is reporting success without progress";

    const auto& stats = fg.stats();
    for (size_t i = 0; i < stats.size(); ++i) {
        EXPECT_EQ(stats[i].successful_procedures, 0u)
            << "block " << i << " reported success while the graph was idle";
    }
}

TEST(IdleProgressContract, IdleMidGraphBlocksParkInsteadOfSpinning) {
    SilentSource source("SilentSource");
    ThroughputBlock<float> throughput("Throughput", kBufferSize);
    FanoutBlock<float> fanout("Fanout", 2, kBufferSize);
    SinkNullBlock<float> sink_a("SinkA", nullptr, nullptr, kBufferSize);
    SinkNullBlock<float> sink_b("SinkB", nullptr, nullptr, kBufferSize);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &throughput.in),
        cler::BlockRunner(&throughput, &fanout.in),
        cler::BlockRunner(&fanout, &sink_a.in, &sink_b.in),
        cler::BlockRunner(&sink_a),
        cler::BlockRunner(&sink_b)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.collect_detailed_stats = true;
    fg.run(config);
    std::this_thread::sleep_for(kIdleWindow);
    fg.stop();

    EXPECT_GT(fg.total_park_events(), 0u)
        << "idle graph never parked; a block is reporting success without progress";

    const auto& stats = fg.stats();
    for (size_t i = 0; i < stats.size(); ++i) {
        EXPECT_EQ(stats[i].successful_procedures, 0u)
            << "block " << i << " reported success while the graph was idle";
    }
}

TEST(IdleProgressContract, EmptyInputYieldsErrorNotSuccess) {
    cler::Channel<float> out_a(kBufferSize);
    cler::Channel<float> out_b(kBufferSize);

    SinkNullBlock<float> sink("SinkNull", nullptr, nullptr, kBufferSize);
    EXPECT_TRUE(sink.procedure().is_err());

    ThroughputBlock<float> throughput("Throughput", kBufferSize);
    EXPECT_TRUE(throughput.procedure(&out_a).is_err());

    FanoutBlock<float> fanout("Fanout", 2, kBufferSize);
    EXPECT_TRUE(fanout.procedure(&out_a, &out_b).is_err());
}
