#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <atomic>
#include <chrono>
#include <thread>

namespace {

constexpr size_t kCapacity = 1 << 14;
constexpr size_t kChunk = 16;

struct SequenceSource : public cler::BlockBase {
    explicit SequenceSource(std::string name) : BlockBase(std::move(name)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<double>* out) {
        if (out->space() < kChunk) {
            return cler::Error::NotEnoughSpace;
        }
        double buf[kChunk];
        for (size_t i = 0; i < kChunk; ++i) buf[i] = static_cast<double>(_next++);
        out->writeN(buf, kChunk);
        return cler::Empty{};
    }

    uint64_t emitted() const { return _next; }

private:
    uint64_t _next = 0;
};

struct ShiftingCostBlock : public cler::BlockBase {
    cler::Channel<double> in;

    ShiftingCostBlock(std::string name, size_t capacity, size_t phase)
        : BlockBase(std::move(name)), in(capacity), _phase(phase) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<double>* out) {
        const size_t n = (std::min)({in.size(), out->space(), kChunk});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto slot = std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 20;
        const bool heavy = ((static_cast<size_t>(slot) + _phase) % 3) == 0;
        const int spins = heavy ? 4000 : 50;
        for (volatile int i = 0; i < spins; ++i) {}

        double buf[kChunk];
        in.readN(buf, n);
        out->writeN(buf, n);
        return cler::Empty{};
    }

private:
    size_t _phase;
};

struct SequenceSink : public cler::BlockBase {
    cler::Channel<double> in;

    SequenceSink(std::string name, size_t capacity) : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t n = (std::min)(in.size(), kChunk);
        if (n == 0) {
            return cler::Error::NotEnoughSamples;
        }
        double buf[kChunk];
        in.readN(buf, n);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t got = static_cast<uint64_t>(buf[i]);
            if (got != _expected) {
                _violations.fetch_add(1, std::memory_order_relaxed);
                _first_bad_expected = _expected;
                _first_bad_got = got;
                _expected = got + 1;
            } else {
                ++_expected;
            }
        }
        _received.store(_expected, std::memory_order_relaxed);
        return cler::Empty{};
    }

    uint64_t received() const { return _received.load(std::memory_order_relaxed); }
    size_t violations() const { return _violations.load(std::memory_order_relaxed); }
    uint64_t first_bad_expected() const { return _first_bad_expected; }
    uint64_t first_bad_got() const { return _first_bad_got; }

private:
    uint64_t _expected = 0;
    uint64_t _first_bad_expected = 0;
    uint64_t _first_bad_got = 0;
    std::atomic<uint64_t> _received{0};
    std::atomic<size_t> _violations{0};
};

}

// Ownership transfer at a repartition barrier must never leave two workers
// owning the same SPSC endpoint. That would not surface as a crash -- it
// surfaces as reordered, duplicated or dropped samples. Drive many
// repartitions under load and assert the stream stays strictly sequential.
TEST(RepartitionStressTest, OwnershipTransferPreservesStreamOrder) {
    SequenceSource source("Source");
    ShiftingCostBlock a("A", kCapacity, 0);
    ShiftingCostBlock b("B", kCapacity, 1);
    ShiftingCostBlock c("C", kCapacity, 2);
    ShiftingCostBlock d("D", kCapacity, 1);
    SequenceSink sink("Sink", kCapacity);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &a.in),
        cler::BlockRunner(&a, &b.in),
        cler::BlockRunner(&b, &c.in),
        cler::BlockRunner(&c, &d.in),
        cler::BlockRunner(&d, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(3);
    config.calibration_ms = 20;
    config.repartition_check_ms = 5;

    fg.run(config);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    fg.stop();

    EXPECT_EQ(sink.violations(), 0u)
        << "stream broke at expected=" << sink.first_bad_expected()
        << " got=" << sink.first_bad_got()
        << " -- overlapping SPSC owners across a repartition barrier";

    EXPECT_GT(sink.received(), 0u) << "no data reached the sink";
    EXPECT_GT(fg.repartition_count(), 1u)
        << "only " << fg.repartition_count()
        << " repartitions; the stress test never exercised the barrier";
}

TEST(RepartitionStressTest, RepeatedRunsRepartitionFromCleanState) {
    for (int iteration = 0; iteration < 3; ++iteration) {
        SequenceSource source("Source");
        ShiftingCostBlock a("A", kCapacity, 0);
        ShiftingCostBlock b("B", kCapacity, 2);
        SequenceSink sink("Sink", kCapacity);

        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &a.in),
            cler::BlockRunner(&a, &b.in),
            cler::BlockRunner(&b, &sink.in),
            cler::BlockRunner(&sink)
        );

        auto config = cler::flowgraph_config::pinned_islands(2);
        config.calibration_ms = 20;
        config.repartition_check_ms = 5;

        fg.run(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        fg.stop();

        EXPECT_EQ(sink.violations(), 0u) << "iteration " << iteration;
        EXPECT_GT(sink.received(), 0u) << "iteration " << iteration;
    }
}
