#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <atomic>
#include <thread>

namespace {

constexpr size_t kCapacity = 4096;

struct CountingSource : public cler::BlockBase {
    static constexpr size_t CHUNK = 32;

    explicit CountingSource(std::string name) : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + CHUNK, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (out->space() < CHUNK) {
            return cler::Error::NotEnoughSpace;
        }
        out->writeN(_buffer, CHUNK);
        _produced.fetch_add(CHUNK, std::memory_order_relaxed);
        return cler::Empty{};
    }

    size_t produced() const { return _produced.load(std::memory_order_relaxed); }

private:
    float _buffer[CHUNK];
    std::atomic<size_t> _produced{0};
};

struct CountingSink : public cler::BlockBase {
    cler::Channel<float> in;

    CountingSink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.commit_read(available);
        _consumed.fetch_add(available, std::memory_order_relaxed);
        return cler::Empty{};
    }

    size_t consumed() const { return _consumed.load(std::memory_order_relaxed); }

private:
    std::atomic<size_t> _consumed{0};
};

struct BlockingSource : public cler::BlockBase {
    static constexpr bool may_block = true;
    static constexpr size_t CHUNK = 8;

    explicit BlockingSource(std::string name) : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + CHUNK, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (out->space() < CHUNK) {
            return cler::Error::NotEnoughSpace;
        }
        out->writeN(_buffer, CHUNK);
        return cler::Empty{};
    }

private:
    float _buffer[CHUNK];
};

struct BlockingSink : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<float> in;

    BlockingSink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.commit_read(available);
        return cler::Empty{};
    }
};

struct PassThrough : public cler::BlockBase {
    cler::Channel<float> in;

    PassThrough(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        const size_t n = (std::min)({in.size(), out->space(), size_t{32}});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        float buf[32];
        in.readN(buf, n);
        out->writeN(buf, n);
        return cler::Empty{};
    }
};

struct FatalSource : public cler::BlockBase {
    explicit FatalSource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>*) {
        return cler::Error::TERM_ProcedureError;
    }
};

std::vector<cler::FlowGraphConfig> all_schedulers() {
    cler::FlowGraphConfig tpb;
    tpb.scheduler = cler::SchedulerType::ThreadPerBlock;

    cler::FlowGraphConfig ftp;
    ftp.scheduler = cler::SchedulerType::FixedThreadPool;
    ftp.num_workers = 2;

    auto pin = cler::flowgraph_config::pinned_islands(2);

    return {tpb, ftp, pin};
}

const char* scheduler_name(const cler::FlowGraphConfig& c) {
    switch (c.scheduler) {
        case cler::SchedulerType::ThreadPerBlock:  return "ThreadPerBlock";
        case cler::SchedulerType::FixedThreadPool: return "FixedThreadPool";
        case cler::SchedulerType::PinnedIslands:   return "PinnedIslands";
    }
    return "?";
}

}

TEST(LifecycleTest, StopBeforeRunIsHarmless) {
    CountingSource source("Source");
    CountingSink sink("Sink", kCapacity);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    fg.stop();
    fg.stop();
    SUCCEED();
}

TEST(LifecycleTest, RepeatedStopIsHarmless) {
    for (const auto& config : all_schedulers()) {
        CountingSource source("Source");
        CountingSink sink("Sink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fg.stop();
        fg.stop();
        fg.stop();

        EXPECT_GT(sink.consumed(), 0u) << scheduler_name(config);
    }
}

TEST(LifecycleTest, RunStopRunWorksForEachScheduler) {
    for (const auto& config : all_schedulers()) {
        CountingSource source("Source");
        CountingSink sink("Sink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fg.stop();
        const size_t after_first = sink.consumed();
        ASSERT_GT(after_first, 0u) << scheduler_name(config);

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fg.stop();

        EXPECT_GT(sink.consumed(), after_first)
            << scheduler_name(config) << " did not resume after a second run()";
    }
}

TEST(LifecycleTest, OnlyMayBlockBlocksStopCleanly) {
    for (const auto& config : all_schedulers()) {
        BlockingSource source("BlockingSource");
        BlockingSink sink("BlockingSink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fg.stop();
        SUCCEED() << scheduler_name(config);
    }
}

TEST(LifecycleTest, FatalErrorCallsTerminateCallbackExactlyOnce) {
    for (const auto& config : all_schedulers()) {
        FatalSource source("FatalSource");
        CountingSink sink("Sink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );

        std::atomic<size_t> calls{0};
        fg.set_on_err_terminate_cb(
            [](void* ctx) { static_cast<std::atomic<size_t>*>(ctx)->fetch_add(1); },
            &calls);

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fg.stop();

        EXPECT_EQ(calls.load(), 1u)
            << scheduler_name(config) << " fired the terminate callback "
            << calls.load() << " times";
    }
}

TEST(LifecycleTest, WorkerCountEdgeValuesAreClamped) {
    for (size_t workers : {size_t{0}, size_t{1}, cler::DEFAULT_MAX_WORKERS,
                           cler::DEFAULT_MAX_WORKERS * 4}) {
        for (auto scheduler : {cler::SchedulerType::FixedThreadPool,
                               cler::SchedulerType::PinnedIslands}) {
            CountingSource source("Source");
            CountingSink sink("Sink", kCapacity);

            auto fg = cler::make_desktop_flowgraph(
                cler::BlockRunner(&source, &sink.in),
                cler::BlockRunner(&sink)
            );

            cler::FlowGraphConfig config;
            config.scheduler = scheduler;
            config.num_workers = workers;
            config.calibration_ms = 60000;

            fg.run(config);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            fg.stop();

            EXPECT_GT(sink.consumed(), 0u)
                << "num_workers=" << workers << " produced no work";
        }
    }
}

TEST(LifecycleTest, DetailedStatsArePopulatedForEachScheduler) {
    for (const auto& base : all_schedulers()) {
        CountingSource source("Source");
        PassThrough a("A", kCapacity);
        PassThrough b("B", kCapacity);
        PassThrough c("C", kCapacity);
        CountingSink sink("Sink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &a.in),
            cler::BlockRunner(&a, &b.in),
            cler::BlockRunner(&b, &c.in),
            cler::BlockRunner(&c, &sink.in),
            cler::BlockRunner(&sink)
        );

        cler::FlowGraphConfig config = base;
        config.collect_detailed_stats = true;
        config.num_workers = 2;

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        fg.stop();

        const auto& stats = fg.stats();
        size_t with_runtime = 0;
        size_t with_successes = 0;
        for (size_t i = 0; i < stats.size(); ++i) {
            if (stats[i].total_runtime_s > 0.0) ++with_runtime;
            if (stats[i].successful_procedures > 0) ++with_successes;
        }

        EXPECT_GT(with_successes, 0u)
            << scheduler_name(base) << " recorded no successful procedures";
        EXPECT_GT(with_runtime, 0u)
            << scheduler_name(base) << " recorded no per-block runtime; the "
               "detailed-stats path in the shared worker loop did not run";
    }
}
