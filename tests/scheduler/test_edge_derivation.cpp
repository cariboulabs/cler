#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/math/gain.hpp"

namespace {

struct DummySource : public cler::BlockBase {
    explicit DummySource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        (void)out;
        return cler::Error::NotEnoughSamples;
    }
};

struct DummySink : public cler::BlockBase {
    cler::Channel<float> in;

    DummySink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }
};

struct HeapChannelBlock : public cler::BlockBase {
    cler::Channel<float>* in;

    HeapChannelBlock(std::string name, size_t capacity) : BlockBase(std::move(name)) {
        in = new cler::Channel<float>(capacity);
    }
    ~HeapChannelBlock() { delete in; }

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }
};

} // namespace

TEST(EdgeDerivationTest, ResolvesContainedOutputs) {
    static constexpr size_t kBufferSize = 4096;

    DummySource source0("Source0");
    DummySource source1("Source1");
    AddBlock<float, 2> add("Add", kBufferSize);
    GainBlock<float> gain("Gain", 2.0f, kBufferSize);
    DummySink sink("Sink", kBufferSize);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source0, &add.in[0]),
        cler::BlockRunner(&source1, &add.in[1]),
        cler::BlockRunner(&add, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );

    ASSERT_EQ(fg.unresolved_edge_count(), 0u);
    ASSERT_EQ(fg.edge_count(), 4u);

    static constexpr uint8_t SOURCE0 = 0, SOURCE1 = 1, ADD = 2, GAIN = 3, SINK = 4;

    const auto& edges = fg.edges();
    EXPECT_EQ(edges[0].producer, SOURCE0);
    EXPECT_EQ(edges[0].consumer, ADD);
    EXPECT_EQ(edges[1].producer, SOURCE1);
    EXPECT_EQ(edges[1].consumer, ADD);
    EXPECT_EQ(edges[2].producer, ADD);
    EXPECT_EQ(edges[2].consumer, GAIN);
    EXPECT_EQ(edges[3].producer, GAIN);
    EXPECT_EQ(edges[3].consumer, SINK);
}

TEST(EdgeDerivationTest, HeapChannelIsUnresolvedAndPartitionsContiguously) {
    static constexpr size_t kBufferSize = 64;

    DummySource source("Source");
    HeapChannelBlock sink("Sink", kBufferSize);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, sink.in),
        cler::BlockRunner(&sink)
    );

    EXPECT_EQ(fg.edge_count(), 0u);
    ASSERT_EQ(fg.unresolved_edge_count(), 1u);
    EXPECT_EQ(fg.unresolved_edges()[0].address, static_cast<const void*>(sink.in));

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.pinned_islands.calibration_ms = 30;
    fg.run_for(std::chrono::milliseconds(120), config);

    EXPECT_EQ(fg.partition().block_count, 2u);
    EXPECT_EQ(fg.partition().island_count, 2u);
}

TEST(EdgeDerivationTest, UnresolvedOutputIsReportedNotEdged) {
    DummySource source("Source");
    cler::Channel<float> orphan(64);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &orphan)
    );

    EXPECT_EQ(fg.edge_count(), 0u);
    ASSERT_EQ(fg.unresolved_edge_count(), 1u);
    EXPECT_EQ(fg.unresolved_edges()[0].producer, 0);
    EXPECT_EQ(fg.unresolved_edges()[0].address, static_cast<const void*>(&orphan));
}
