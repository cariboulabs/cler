#include <gtest/gtest.h>
#include <thread>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

namespace {

constexpr size_t CHUNK = 64;

struct SteadySource : public cler::BlockBase {
    explicit SteadySource(std::string name) : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + CHUNK, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (out->space() < CHUNK) return cler::Error::NotEnoughSpace;
        out->writeN(_buffer, CHUNK);
        return cler::Empty{};
    }

private:
    float _buffer[CHUNK];
};

template<size_t WorkIterations>
struct WeightedStage : public cler::BlockBase {
    cler::Channel<float> in;

    WeightedStage(std::string name, size_t capacity, const std::atomic<bool>* heavy_gate = nullptr)
        : BlockBase(std::move(name)), in(capacity), _heavy_gate(heavy_gate) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        const size_t n = (std::min)((std::min)(in.size(), out->space()), CHUNK);
        if (n == 0) return cler::Error::NotEnoughSpaceOrSamples;
        in.readN(_buffer, n);
        const size_t iterations =
            (_heavy_gate && !_heavy_gate->load(std::memory_order_relaxed)) ? 1 : WorkIterations;
        for (size_t w = 0; w < iterations; ++w) {
            for (size_t i = 0; i < n; ++i) {
                _accumulator += _buffer[i] * 1.000001f;
            }
        }
        out->writeN(_buffer, n);
        return cler::Empty{};
    }

    volatile float _accumulator = 0.0f;

private:
    const std::atomic<bool>* _heavy_gate;
    float _buffer[CHUNK];
};

struct CountingSink : public cler::BlockBase {
    cler::Channel<float> in;

    CountingSink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t available = in.size();
        if (available == 0) return cler::Error::NotEnoughSamples;
        in.commit_read(available);
        received.fetch_add(available, std::memory_order_relaxed);
        return cler::Empty{};
    }

    std::atomic<size_t> received{0};
};

struct BurstySource : public cler::BlockBase {
    BurstySource(std::string name, std::chrono::microseconds period, size_t burst)
        : BlockBase(std::move(name)), _period(period), _burst(burst),
          _next_release(std::chrono::steady_clock::now()) {
        std::fill(_buffer, _buffer + CHUNK, 2.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (_remaining_in_burst == 0) {
            if (std::chrono::steady_clock::now() < _next_release) return cler::Error::NotEnoughSamples;
            _next_release += _period;
            _remaining_in_burst = _burst;
        }
        const size_t n = (std::min)(_remaining_in_burst, (std::min)(out->space(), CHUNK));
        if (n == 0) return cler::Error::NotEnoughSpace;
        out->writeN(_buffer, n);
        _remaining_in_burst -= n;
        emitted.fetch_add(n, std::memory_order_relaxed);
        return cler::Empty{};
    }

    std::atomic<size_t> emitted{0};

private:
    std::chrono::microseconds _period;
    size_t _burst;
    size_t _remaining_in_burst = 0;
    std::chrono::steady_clock::time_point _next_release;
    float _buffer[CHUNK];
};

struct CostShiftOutcome {
    size_t repartitions;
    size_t island_of_upstream_heavy;
    size_t island_of_downstream_heavy;
    size_t received;
};

CostShiftOutcome run_cost_shift(size_t repartition_check_ms) {
    static constexpr size_t CAPACITY = 1 << 14;
    static constexpr uint8_t UPSTREAM_HEAVY = 3;
    static constexpr uint8_t DOWNSTREAM_HEAVY = 4;

    std::atomic<bool> early_heavy{true};
    std::atomic<bool> late_heavy{false};

    SteadySource source("Source");
    WeightedStage<400> early0("Early0", CAPACITY, &early_heavy);
    WeightedStage<400> early1("Early1", CAPACITY, &early_heavy);
    WeightedStage<400> late0("Late0", CAPACITY, &late_heavy);
    WeightedStage<400> late1("Late1", CAPACITY, &late_heavy);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &early0.in),
        cler::BlockRunner(&early0, &early1.in),
        cler::BlockRunner(&early1, &late0.in),
        cler::BlockRunner(&late0, &late1.in),
        cler::BlockRunner(&late1, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 150;
    config.repartition_check_ms = repartition_check_ms;

    fg.run(config);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    early_heavy.store(false);
    late_heavy.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    fg.stop();

    const auto& partition = fg.partition();
    return CostShiftOutcome{fg.repartition_count(),
                            partition.island_of(UPSTREAM_HEAVY),
                            partition.island_of(DOWNSTREAM_HEAVY),
                            sink.received.load()};
}

} // namespace

TEST(PinnedIslandsTest, CalibrationIsolatesHeavyBlock) {
    static constexpr size_t CAPACITY = 1 << 14;

    SteadySource source("Source");
    WeightedStage<400> heavy("Heavy", CAPACITY);
    WeightedStage<1> light0("Light0", CAPACITY);
    WeightedStage<1> light1("Light1", CAPACITY);
    WeightedStage<1> light2("Light2", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &heavy.in),
        cler::BlockRunner(&heavy, &light0.in),
        cler::BlockRunner(&light0, &light1.in),
        cler::BlockRunner(&light1, &light2.in),
        cler::BlockRunner(&light2, &sink.in),
        cler::BlockRunner(&sink)
    );

    ASSERT_EQ(fg.unresolved_edge_count(), 0u);

    auto config = cler::flowgraph_config::pinned_islands(3);
    config.calibration_ms = 150;

    static constexpr uint8_t HEAVY = 1;

    fg.run_for(std::chrono::milliseconds(600), config);

    ASSERT_GT(sink.received.load(), 0u);
    ASSERT_EQ(fg.repartition_count(), 1u);

    const auto& partition = fg.partition();
    ASSERT_EQ(partition.island_count, 3u);
    ASSERT_EQ(partition.block_count, 6u);

    for (uint8_t i = 0; i < 6; ++i) {
        EXPECT_EQ(partition.block_ids[i], i);
    }

    const size_t heavy_island = partition.island_of(HEAVY);
    ASSERT_LT(heavy_island, partition.island_count);
    EXPECT_EQ(partition.island_size(heavy_island), 1u);
}

TEST(PinnedIslandsTest, FallbackPartitionSplitsByCountNotCost) {
    static constexpr size_t CAPACITY = 1 << 14;

    SteadySource source("Source");
    WeightedStage<400> heavy("Heavy", CAPACITY);
    WeightedStage<1> light0("Light0", CAPACITY);
    WeightedStage<1> light1("Light1", CAPACITY);
    WeightedStage<1> light2("Light2", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &heavy.in),
        cler::BlockRunner(&heavy, &light0.in),
        cler::BlockRunner(&light0, &light1.in),
        cler::BlockRunner(&light1, &light2.in),
        cler::BlockRunner(&light2, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(3);
    config.calibration_ms = 60000;

    fg.run_for(std::chrono::milliseconds(150), config);

    ASSERT_EQ(fg.repartition_count(), 0u);

    const auto& partition = fg.partition();
    ASSERT_EQ(partition.island_count, 3u);
    EXPECT_EQ(partition.island_size(0), 2u);
    EXPECT_EQ(partition.island_size(1), 2u);
    EXPECT_EQ(partition.island_size(2), 2u);
    EXPECT_EQ(partition.island_of(1), 0u);
}

TEST(PinnedIslandsTest, BurstyFlowParksWithoutDeadlock) {
    static constexpr size_t CAPACITY = 1 << 14;
    static constexpr size_t BURST = 4096;
    static constexpr auto PERIOD = std::chrono::microseconds(5000);
    static constexpr auto RUN_TIME = std::chrono::seconds(10);

    BurstySource source("Bursty", PERIOD, BURST);
    WeightedStage<1> stage0("Stage0", CAPACITY);
    WeightedStage<1> stage1("Stage1", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(3);
    config.calibration_ms = 200;

    const auto started = std::chrono::steady_clock::now();
    fg.run_for(RUN_TIME, config);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_LT(elapsed, RUN_TIME + std::chrono::seconds(2));
    EXPECT_TRUE(fg.is_stopped());

    const size_t emitted = source.emitted.load();
    const size_t received = sink.received.load();

    EXPECT_GT(fg.total_park_events(), 0u);
    EXPECT_GT(emitted, BURST * 1000u);
    EXPECT_GT(received, emitted - 4 * CAPACITY);
}

TEST(PinnedIslandsTest, DedicatedMayBlockThreadWakesParkedWorkers) {
    static constexpr size_t CAPACITY = 1 << 14;
    static constexpr size_t BURST = 2048;
    static constexpr auto PERIOD = std::chrono::microseconds(5000);

    BurstySource source("Bursty", PERIOD, BURST);
    WeightedStage<1> stage0("Stage0", CAPACITY);
    WeightedStage<1> stage1("Stage1", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunnerMayBlock(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(3);
    config.calibration_ms = 100;

    fg.run_for(std::chrono::seconds(2), config);

    EXPECT_EQ(fg.partition().block_count, 3u);
    EXPECT_GT(fg.total_park_events(), 0u);
    EXPECT_GT(source.emitted.load(), BURST * 100u);
    EXPECT_GT(sink.received.load(), source.emitted.load() - 4 * CAPACITY);
}

TEST(PinnedIslandsTest, DegenerateWorkerCountsStayBounded) {
    static constexpr size_t CAPACITY = 1 << 12;

    for (size_t workers : {size_t{1}, size_t{64}}) {
        SteadySource source("Source");
        WeightedStage<1> stage("Stage", CAPACITY);
        CountingSink sink("Sink", CAPACITY);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &stage.in),
            cler::BlockRunner(&stage, &sink.in),
            cler::BlockRunner(&sink)
        );

        auto config = cler::flowgraph_config::pinned_islands(workers);
        config.calibration_ms = 50;

        fg.run_for(std::chrono::milliseconds(200), config);

        EXPECT_GT(sink.received.load(), 0u);
        EXPECT_EQ(fg.partition().island_count, (std::min)(workers, size_t{3}));
        EXPECT_EQ(fg.partition().block_count, 3u);
    }
}

TEST(PinnedIslandsTest, PeriodicCheckFollowsCostShift) {
    const CostShiftOutcome outcome = run_cost_shift(100);

    EXPECT_GT(outcome.received, 0u);
    EXPECT_GE(outcome.repartitions, 2u);
    EXPECT_EQ(outcome.island_of_upstream_heavy, 0u);
    EXPECT_EQ(outcome.island_of_downstream_heavy, 1u);
}

TEST(PinnedIslandsTest, PeriodicCheckDisabledIgnoresCostShift) {
    const CostShiftOutcome outcome = run_cost_shift(0);

    EXPECT_GT(outcome.received, 0u);
    EXPECT_EQ(outcome.repartitions, 1u);
    EXPECT_EQ(outcome.island_of_upstream_heavy, outcome.island_of_downstream_heavy);
}

TEST(PinnedIslandsTest, HysteresisKeepsSteadyChainPartitioned) {
    static constexpr size_t CAPACITY = 1 << 14;

    SteadySource source("Source");
    WeightedStage<8> stage0("Stage0", CAPACITY);
    WeightedStage<8> stage1("Stage1", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 200;
    config.repartition_check_ms = 100;

    fg.run_for(std::chrono::seconds(2), config);

    EXPECT_GT(sink.received.load(), 0u);
    EXPECT_EQ(fg.repartition_count(), 1u);
}

TEST(PinnedIslandsTest, MatchesFixedThreadPoolOnSteadyChain) {
    static constexpr size_t CAPACITY = 1 << 14;

    SteadySource source("Source");
    WeightedStage<4> stage0("Stage0", CAPACITY);
    WeightedStage<4> stage1("Stage1", CAPACITY);
    CountingSink sink("Sink", CAPACITY);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    config.calibration_ms = 100;
    config.collect_detailed_stats = true;

    fg.run_for(std::chrono::milliseconds(500), config);

    EXPECT_GT(sink.received.load(), 0u);
    EXPECT_EQ(fg.affinity_failure_count(), 0u);
    EXPECT_EQ(fg.partition().island_count, 2u);
}
