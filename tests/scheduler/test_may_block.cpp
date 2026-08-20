#include <gtest/gtest.h>
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

namespace {

struct FastSource : public cler::BlockBase {
    explicit FastSource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (out->space() < 1) return cler::Error::NotEnoughSpace;
        out->push(1.0f);
        return cler::Empty{};
    }
};

struct BlockingSource : public cler::BlockBase {
    static constexpr bool may_block = true;

    explicit BlockingSource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (out->space() < 1) return cler::Error::NotEnoughSpace;
        out->push(2.0f);
        return cler::Empty{};
    }
};

struct DrainingSink : public cler::BlockBase {
    cler::Channel<float> in;

    DrainingSink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        float v;
        if (!in.try_pop(v)) return cler::Error::NotEnoughSamples;
        received++;
        return cler::Empty{};
    }

    size_t received = 0;
};

} // namespace

TEST(MayBlockTest, StaticMemberIsAutoDetected) {
    FastSource fast("fast");
    BlockingSource blocking("blocking");
    DrainingSink sink0("sink0", 1 << 12);
    DrainingSink sink1("sink1", 1 << 12);

    auto runner_fast = cler::BlockRunner(&fast, &sink0.in);
    auto runner_blocking = cler::BlockRunner(&blocking, &sink1.in);

    EXPECT_FALSE(runner_fast.may_block);
    EXPECT_TRUE(runner_blocking.may_block);
}

TEST(MayBlockTest, ManualOverrideForcesFlag) {
    FastSource fast("fast");
    DrainingSink sink("sink", 1 << 12);

    auto runner = cler::BlockRunnerMayBlock(&fast, &sink.in);
    EXPECT_TRUE(runner.may_block);
}

TEST(MayBlockTest, BlockingBlockGetsDedicatedThreadInFixedThreadPool) {
    FastSource fast0("fast0");
    FastSource fast1("fast1");
    FastSource fast2("fast2");
    FastSource fast3("fast3");
    BlockingSource blocking("blocking");
    DrainingSink sink0("sink0", 1 << 16);
    DrainingSink sink1("sink1", 1 << 16);
    DrainingSink sink2("sink2", 1 << 16);
    DrainingSink sink3("sink3", 1 << 16);
    DrainingSink sink_blocking("sink_blocking", 1 << 16);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&fast0, &sink0.in),
        cler::BlockRunner(&fast1, &sink1.in),
        cler::BlockRunner(&fast2, &sink2.in),
        cler::BlockRunner(&fast3, &sink3.in),
        cler::BlockRunnerMayBlock(&blocking, &sink_blocking.in),
        cler::BlockRunner(&sink0),
        cler::BlockRunner(&sink1),
        cler::BlockRunner(&sink2),
        cler::BlockRunner(&sink3),
        cler::BlockRunner(&sink_blocking)
    );

    ASSERT_EQ(fg.unresolved_edge_count(), 0u);

    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::FixedThreadPool;
    config.num_workers = 2;

    fg.run_for(std::chrono::milliseconds(100), config);

    // loaded CI runners deliver far fewer samples in the 100 ms window; the
    // point is regular sinks stream freely while the blocking sink trickles
    EXPECT_GT(sink0.received, 10000u);
    EXPECT_GT(sink1.received, 10000u);
    EXPECT_GT(sink2.received, 10000u);
    EXPECT_GT(sink3.received, 10000u);

    EXPECT_GT(sink_blocking.received, 0u);
    EXPECT_LT(sink_blocking.received, 100u);
}
