// Idle-contract audit test.
//
// The cler scheduler treats a returned cler::Empty{} as "this block made
// progress" and keeps the worker hot. A block that did NOT move data (read 0
// samples and wrote 0) must instead return a no-progress error
// (NotEnoughSamples / NotEnoughSpace), or it lies to the scheduler and spins a
// core for nothing. See docs/scheduler_latency_stress_plan.md (Idle Contract
// Audit) and docs/scheduler_latency_impl_plan.md Phase 1.
//
// These tests exercise the pure-software blocks that were found violating the
// contract. Hardware / file / socket blocks (source_cariboulite, source_hackrf,
// source_file, source_audio_file, udp/*) were fixed in the same change but need
// real resources to drive and are not unit-tested here.

#include <gtest/gtest.h>

#include "cler.hpp"
#include "desktop_blocks/utils/throughput.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"

class IdleContractTest : public ::testing::Test {};

// A block with no input available must report no progress, not success.
TEST_F(IdleContractTest, ThroughputReportsNoProgressWhenInputEmpty) {
    const size_t buffer_size = 1024;
    ThroughputBlock<float> block("throughput", buffer_size);
    cler::Channel<float> out(buffer_size);

    auto result = block.procedure(&out);  // input empty -> nothing to transfer
    EXPECT_TRUE(result.is_err())
        << "ThroughputBlock returned success while doing zero work";
}

TEST_F(IdleContractTest, FanoutReportsNoProgressWhenInputEmpty) {
    const size_t buffer_size = 1024;
    FanoutBlock<float> block("fanout", 2, buffer_size);
    cler::Channel<float> out1(buffer_size), out2(buffer_size);

    auto result = block.procedure(&out1, &out2);  // input empty
    EXPECT_TRUE(result.is_err())
        << "FanoutBlock returned success while doing zero work";
}

TEST_F(IdleContractTest, SinkNullReportsNoProgressWhenInputEmpty) {
    const size_t buffer_size = 1024;
    SinkNullBlock<float> block("sink_null", nullptr, nullptr, buffer_size);

    auto result = block.procedure();  // input empty -> commits nothing
    EXPECT_TRUE(result.is_err())
        << "SinkNullBlock returned success while consuming zero samples";
}

// Sanity: when there IS work, the same blocks must still report success, so the
// fix did not turn real progress into a spurious no-progress.
TEST_F(IdleContractTest, ThroughputReportsSuccessWhenWorkDone) {
    const size_t buffer_size = 1024;
    ThroughputBlock<float> block("throughput", buffer_size);
    cler::Channel<float> out(buffer_size);
    for (int i = 0; i < 8; ++i) block.in.push(static_cast<float>(i));

    auto result = block.procedure(&out);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(out.size(), 8u);
}

TEST_F(IdleContractTest, SinkNullReportsSuccessWhenWorkDone) {
    const size_t buffer_size = 1024;
    SinkNullBlock<float> block("sink_null", nullptr, nullptr, buffer_size);
    for (int i = 0; i < 8; ++i) block.in.push(static_cast<float>(i));

    auto result = block.procedure();
    EXPECT_TRUE(result.is_ok());
}
