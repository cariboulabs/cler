#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "cler.hpp"
#include "desktop_blocks/triggers/trigger_block.hpp"

namespace {

using Trig = TriggerBlock<float>;

constexpr float kFloorDb = -100.0f;
constexpr float kBurstDb = 0.0f;
constexpr float kLevelDb = -50.0f;

struct Frame {
    std::vector<float> y;
    float  pre_ms = 0.0f, post_ms = 0.0f;
    size_t trig_idx = 0, rate = 0, stride = 1;
    unsigned long count = 0;
};

// Streamlined drive: no flowgraph, no threads, so procedure() runs exactly
// where we can observe it. render() is never called, so no ImGui context.
template <typename Gen>
void feed(Trig& t, size_t n, Gen gen) {
    std::vector<float> buf(4096);
    size_t i = 0;
    while (i < n) {
        const size_t chunk = std::min(buf.size(), n - i);
        for (size_t k = 0; k < chunk; ++k) buf[k] = gen(i + k);
        size_t w = 0;
        while (w < chunk) {
            const size_t space = t.in.space();
            if (space == 0) { (void)t.procedure(); continue; }
            const size_t c = std::min(space, chunk - w);
            t.in.writeN(buf.data() + w, c);
            w += c;
            (void)t.procedure();
        }
        i += chunk;
    }
    while (t.in.size() > 0) (void)t.procedure();
}

bool grab(Trig& t, Frame& f) {
    return t.export_frame(f.y, f.pre_ms, f.post_ms, f.trig_idx, f.count,
                          f.rate, f.stride);
}

// A step from the noise floor up to a burst level, starting at `edge`.
auto step_at(size_t edge) {
    return [edge](size_t i) { return i < edge ? kFloorDb : kBurstDb; };
}

// Short window that fits the point budget: byte-for-byte the old raw behaviour.
TEST(TriggerBlock, ShortWindowCapturesRawSamples) {
    Trig t("T", 1'000'000, kLevelDb, /*window_ms*/10.0f, /*pre%*/10.0f,
           /*holdoff_ms*/100.0f, Trig::Edge::Rising, Trig::Mode::Normal,
           /*hyst*/3.0f, /*auto_ms*/200.0f, /*max_window_ms*/1000.0f);

    feed(t, 40'000, step_at(5'000));

    Frame f;
    ASSERT_TRUE(grab(t, f));
    EXPECT_EQ(f.stride, 1u);                 // raw, no envelope
    EXPECT_EQ(f.y.size(), 10'000u);          // 10 ms at 1 MS/s
    EXPECT_EQ(f.trig_idx, 1'000u);           // 10% pre-trigger, ring was full
    EXPECT_FLOAT_EQ(f.y.front(), kFloorDb);  // pre-trigger is the floor
    EXPECT_FLOAT_EQ(f.y[f.trig_idx], kBurstDb);
}

// The regression this whole change is about: the ms ceiling used to be a fixed
// SAMPLE capacity, so raising the rate shrank it (5 Msamples / 20 MS/s = 250 ms).
TEST(TriggerBlock, WindowCeilingDoesNotShrinkWithSampleRate) {
    Trig t("T", 1'000'000, kLevelDb, 20.0f, 10.0f, 100.0f,
           Trig::Edge::Rising, Trig::Mode::Auto, 3.0f, 200.0f,
           /*max_window_ms*/5000.0f);

    EXPECT_FLOAT_EQ(t.max_window_ms(), 5000.0f);

    // Same live rate change that produced the 250 ms ceiling before.
    t.set_config(kLevelDb, 5000.0f, 10.0f, 100.0f, Trig::Edge::Rising,
                 Trig::Mode::Auto, 3.0f, 200.0f, /*rate*/20'000'000);

    EXPECT_FLOAT_EQ(t.max_window_ms(), 5000.0f);
    // 5 s at 20 MS/s cannot be stored raw, so it is stored coarser instead.
    EXPECT_GT(t.stride_for_window_ms(5000.0f, 20'000'000), 1u);
    // ...and still bounded by the point budget, not by 1.6 GB of samples.
    EXPECT_LE(t.capture_points(), 2u * 1024 * 1024);
}

// Window past the budget: stored as an envelope, never truncated in time.
TEST(TriggerBlock, LongWindowStoresEnvelopeWithinBudget) {
    // ctor rate sizes the budget at 20000 points; the live rate is 10x higher.
    Trig t("T", 100'000, kLevelDb, /*window_ms*/200.0f, /*pre%*/10.0f,
           /*holdoff_ms*/10'000.0f, Trig::Edge::Rising, Trig::Mode::Normal,
           3.0f, 200.0f, /*max_window_ms*/200.0f);
    const size_t budget = t.capture_points();
    ASSERT_EQ(budget, 20'000u);

    t.set_config(kLevelDb, 200.0f, 10.0f, 10'000.0f, Trig::Edge::Rising,
                 Trig::Mode::Normal, 3.0f, 200.0f, /*rate*/1'000'000);

    const size_t window = 200'000;   // 200 ms at 1 MS/s
    feed(t, 40'000, [](size_t) { return kFloorDb; });
    feed(t, 260'000, step_at(0));

    Frame f;
    ASSERT_TRUE(grab(t, f));
    EXPECT_EQ(f.stride, 20u);                 // ceil(200000 / (20000/2))
    EXPECT_LE(f.y.size(), budget);            // never exceeds the budget
    EXPECT_EQ(f.rate, 1'000'000u);

    // The frame still spans the full 200 ms: last point maps to ~window.
    const size_t last = Trig::frame_input_index(f.y.size() - 1, f.stride);
    EXPECT_GE(last, window - f.stride);
    EXPECT_LE(last, window);
    EXPECT_FLOAT_EQ(f.pre_ms + f.post_ms, 200.0f);
}

// Detection must stay at full rate: a burst far shorter than one stored bucket
// still fires, and its peak survives into the envelope rather than averaging out.
TEST(TriggerBlock, SubBucketBurstTriggersAndSurvivesInEnvelope) {
    Trig t("T", 100'000, kLevelDb, 200.0f, 10.0f, 10'000.0f,
           Trig::Edge::Rising, Trig::Mode::Normal, 3.0f, 200.0f, 200.0f);
    t.set_config(kLevelDb, 200.0f, 10.0f, 10'000.0f, Trig::Edge::Rising,
                 Trig::Mode::Normal, 3.0f, 200.0f, /*rate*/1'000'000);

    // One single sample above the level, inside a 20-sample bucket.
    const size_t spike = 60'000;
    feed(t, 300'000, [spike](size_t i) { return i == spike ? kBurstDb : kFloorDb; });

    Frame f;
    ASSERT_TRUE(grab(t, f));
    EXPECT_GE(f.count, 1u);              // the 1-sample burst did fire it
    EXPECT_EQ(f.stride, 20u);
    // A max-hold slot kept the peak; an averaging reduction would have buried it.
    EXPECT_FLOAT_EQ(*std::max_element(f.y.begin(), f.y.end()), kBurstDb);
}

// The x-mapping the plot and the .dat export both go through.
TEST(TriggerBlock, FrameInputIndexIsMonotonicAndSpansBuckets) {
    for (size_t i = 0; i < 8; ++i) EXPECT_EQ(Trig::frame_input_index(i, 1), i);

    size_t prev = 0;
    for (size_t i = 0; i < 16; ++i) {
        const size_t idx = Trig::frame_input_index(i, 10);
        if (i) EXPECT_GT(idx, prev);     // strictly increasing: min then max
        prev = idx;
    }
    EXPECT_EQ(Trig::frame_input_index(0, 10), 0u);   // bucket 0 min
    EXPECT_EQ(Trig::frame_input_index(1, 10), 9u);   // bucket 0 max
    EXPECT_EQ(Trig::frame_input_index(2, 10), 10u);  // bucket 1 min
}

}  // namespace
