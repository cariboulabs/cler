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
