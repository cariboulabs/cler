#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

namespace {

struct FixedRateSource : public cler::BlockBase {
    static constexpr size_t ITEMS_PER_CALL = 32;

    explicit FixedRateSource(std::string name) : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + ITEMS_PER_CALL, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (out->space() < ITEMS_PER_CALL) {
            return cler::Error::NotEnoughSpace;
        }
        const size_t written = out->writeN(_buffer, ITEMS_PER_CALL);
        if (written != ITEMS_PER_CALL) {
            return cler::Error::NotEnoughSpace;
        }
        return cler::Empty{};
    }

private:
    float _buffer[ITEMS_PER_CALL];
};

struct DrainingSink : public cler::BlockBase {
    cler::Channel<float> in;

    DrainingSink(std::string name, size_t capacity)
        : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.commit_read(available);
        _received += available;
        return cler::Empty{};
    }

    size_t received() const { return _received; }

private:
    size_t _received = 0;
};

struct FanoutLikeBlock : public cler::BlockBase {
    static constexpr size_t CHUNK = 32;
    static constexpr size_t NUM_OUTS = 3;

    cler::Channel<float> in;

    FanoutLikeBlock(std::string name, size_t capacity)
        : BlockBase(std::move(name)), in(capacity) {}

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        if (in.size() < CHUNK) {
            return cler::Error::NotEnoughSamples;
        }
        if (std::min({outs->space()...}) < CHUNK) {
            return cler::Error::NotEnoughSpace;
        }
        in.readN(_buffer, CHUNK);
        ((outs->writeN(_buffer, CHUNK)), ...);
        return cler::Empty{};
    }

private:
    float _buffer[CHUNK];
};

} // namespace

TEST(SchedulerCostTest, BlockCostsReflectActualCallShape) {
    FixedRateSource source("Source");
    DrainingSink sink("Sink", 1 << 16);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 60000;
    fg.run_for(std::chrono::milliseconds(200), config);

    ASSERT_GT(sink.received(), 0u);

    const auto costs = fg.block_costs();

    EXPECT_GT(costs[0].ewma_ns_per_call, 0.0);
    EXPECT_LT(costs[0].ewma_ns_per_call, 1e9);

    EXPECT_GT(costs[0].ewma_items_per_call, FixedRateSource::ITEMS_PER_CALL * 0.5);
    EXPECT_LE(costs[0].ewma_items_per_call, FixedRateSource::ITEMS_PER_CALL + 1e-6);
}

TEST(SchedulerCostTest, SinkWeightIsMeasuredInItemsNotCalls) {
    FixedRateSource source("Source");
    DrainingSink sink("Sink", 1 << 16);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 60000;
    fg.run_for(std::chrono::milliseconds(200), config);

    ASSERT_GT(sink.received(), 0u);

    const auto costs = fg.block_costs();
    EXPECT_GT(costs[1].ewma_items_per_call, 0.0)
        << "sink reports zero items/call; its weight collapses to ns/call";
    EXPECT_GT(costs[1].ewma_ns_per_call, 0.0);
}

TEST(SchedulerCostTest, MultiOutputWeightDoesNotShrinkWithOutputCount) {
    static constexpr size_t kCapacity = 1 << 16;

    FixedRateSource source("Source");
    FanoutLikeBlock fanout("Fanout", kCapacity);
    DrainingSink sink_a("SinkA", kCapacity);
    DrainingSink sink_b("SinkB", kCapacity);
    DrainingSink sink_c("SinkC", kCapacity);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &sink_a.in, &sink_b.in, &sink_c.in),
        cler::BlockRunner(&sink_a),
        cler::BlockRunner(&sink_b),
        cler::BlockRunner(&sink_c)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 60000;
    fg.run_for(std::chrono::milliseconds(300), config);

    ASSERT_GT(sink_a.received(), 0u);

    const auto costs = fg.block_costs();
    const double items = costs[1].ewma_items_per_call;

    EXPECT_GT(items, FanoutLikeBlock::CHUNK * 0.5);
    EXPECT_LT(items, FanoutLikeBlock::CHUNK * 1.5)
        << "multi-output block weighted by summed output writes ("
        << FanoutLikeBlock::CHUNK * FanoutLikeBlock::NUM_OUTS << " expected if summing)";
}

namespace {

struct TwoInputSkewedBlock : public cler::BlockBase {
    static constexpr size_t FAST_CHUNK = 32;
    static constexpr size_t SLOW_CHUNK = 1;
    static constexpr size_t MAX_CALLS = 1500;

    cler::Channel<float> in_lead;
    cler::Channel<float> in_fast;

    TwoInputSkewedBlock(std::string name, size_t capacity)
        : BlockBase(std::move(name)), in_lead(capacity), in_fast(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (_calls >= MAX_CALLS) {
            return cler::Error::NotEnoughSamples;
        }
        if (in_lead.size() < SLOW_CHUNK || in_fast.size() < FAST_CHUNK) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() < FAST_CHUNK) {
            return cler::Error::NotEnoughSpace;
        }
        in_lead.readN(_buffer, SLOW_CHUNK);
        in_fast.readN(_buffer, FAST_CHUNK);
        out->writeN(_buffer, FAST_CHUNK);
        ++_calls;
        return cler::Empty{};
    }

private:
    float _buffer[FAST_CHUNK];
    size_t _calls = 0;
};

}

TEST(SchedulerCostTest, SkewedMultiInputUsesMaxDeltaNotDeltaOfMaxima) {
    static constexpr size_t kCapacity = 1 << 16;
    static constexpr size_t kHeadStart = 60000;

    FixedRateSource source_lead("SourceLead");
    FixedRateSource source_fast("SourceFast");
    TwoInputSkewedBlock skewed("Skewed", kCapacity);
    DrainingSink sink("Sink", kCapacity);

    std::vector<float> filler(kHeadStart, 1.0f);
    ASSERT_EQ(skewed.in_lead.writeN(filler.data(), kHeadStart), kHeadStart);
    ASSERT_EQ(skewed.in_lead.readN(filler.data(), kHeadStart), kHeadStart);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_lead, &skewed.in_lead),
        cler::BlockRunner(&source_fast, &skewed.in_fast),
        cler::BlockRunner(&skewed, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 60000;
    fg.run_for(std::chrono::milliseconds(300), config);

    ASSERT_GT(sink.received(), 0u);

    const auto costs = fg.block_costs();
    EXPECT_GT(costs[2].ewma_items_per_call, TwoInputSkewedBlock::FAST_CHUNK * 0.5)
        << "in_lead holds the larger lifetime count but advances only "
        << TwoInputSkewedBlock::SLOW_CHUNK << "/call; subtracting maxima of lifetime "
           "counters reports that instead of the "
        << TwoInputSkewedBlock::FAST_CHUNK << "/call actually moved";
}

namespace {

struct UnreadInputBlock : public cler::BlockBase {
    static constexpr size_t CHUNK = 32;

    cler::Channel<float> ctrl;

    UnreadInputBlock(std::string name, size_t capacity)
        : BlockBase(std::move(name)), ctrl(capacity) {
        std::fill(_buffer, _buffer + CHUNK, 2.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (out->space() < CHUNK) {
            return cler::Error::NotEnoughSpace;
        }
        out->writeN(_buffer, CHUNK);
        return cler::Empty{};
    }

private:
    float _buffer[CHUNK];
};

}

TEST(SchedulerCostTest, ResolvedButUnreadInputFallsBackToOutputWrites) {
    static constexpr size_t kCapacity = 1 << 16;

    FixedRateSource ctrl_source("CtrlSource");
    UnreadInputBlock work("Work", kCapacity);
    DrainingSink sink("Sink", kCapacity);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&ctrl_source, &work.ctrl),
        cler::BlockRunner(&work, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 60000;
    fg.run_for(std::chrono::milliseconds(300), config);

    ASSERT_GT(sink.received(), 0u);

    const auto costs = fg.block_costs();
    EXPECT_GT(costs[1].ewma_items_per_call, UnreadInputBlock::CHUNK * 0.5)
        << "block has a resolved input it never reads; reporting zero items would "
           "silently weight it in ns/call while every other block is ns/item";
}
