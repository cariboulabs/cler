#include <gtest/gtest.h>
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/gain.hpp"
#include "desktop_blocks/utils/fused.hpp"
#include <random>
#include <vector>

namespace {

struct VectorSource : public cler::BlockBase {
    VectorSource(std::string name, std::vector<float> samples)
        : BlockBase(std::move(name)), _samples(std::move(samples)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (_offset >= _samples.size()) {
            return cler::Error::NotEnoughSamples;
        }
        auto [write_ptr, write_space] = out->write_dbf();
        size_t transferable = std::min(write_space, _samples.size() - _offset);
        if (transferable == 0) {
            return cler::Error::NotEnoughSpace;
        }
        std::copy(_samples.begin() + _offset, _samples.begin() + _offset + transferable, write_ptr);
        _offset += transferable;
        out->commit_write(transferable);
        return cler::Empty{};
    }

private:
    std::vector<float> _samples;
    size_t _offset = 0;
};

struct VectorSink : public cler::BlockBase {
    cler::Channel<float> in;

    VectorSink(std::string name, size_t capacity)
        : BlockBase(std::move(name)), in(capacity) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        _received.insert(_received.end(), read_ptr, read_ptr + read_size);
        in.commit_read(read_size);
        return cler::Empty{};
    }

    const std::vector<float>& received() const { return _received; }

private:
    std::vector<float> _received;
};

std::vector<float> make_random_buffer(size_t n) {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    std::vector<float> buffer(n);
    for (auto& v : buffer) v = dist(rng);
    return buffer;
}

}

TEST(FusedBlockTest, MatchesSequentialGainChain) {
    static constexpr size_t kBufferSize = 4096;
    auto samples = make_random_buffer(2000);

    GainBlock<float> gain0("Gain0", 2.0f, kBufferSize);
    GainBlock<float> gain1("Gain1", 0.5f, kBufferSize);

    VectorSource source_chain("SourceChain", samples);
    VectorSink sink_chain("SinkChain", kBufferSize);

    auto fg_chain = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_chain, &gain0.in),
        cler::BlockRunner(&gain0, &gain1.in),
        cler::BlockRunner(&gain1, &sink_chain.in),
        cler::BlockRunner(&sink_chain)
    );
    fg_chain.run();
    while (sink_chain.received().size() < samples.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fg_chain.stop();

    GainBlock<float> fused_gain0("FusedGain0", 2.0f, kBufferSize);
    GainBlock<float> fused_gain1("FusedGain1", 0.5f, kBufferSize);
    FusedBlock<GainBlock<float>, GainBlock<float>> fused("Fused", &fused_gain0, &fused_gain1, kBufferSize);

    VectorSource source_fused("SourceFused", samples);
    VectorSink sink_fused("SinkFused", kBufferSize);

    auto fg_fused = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_fused, &fused.in),
        cler::BlockRunner(&fused, &sink_fused.in),
        cler::BlockRunner(&sink_fused)
    );
    fg_fused.run();
    while (sink_fused.received().size() < samples.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fg_fused.stop();

    ASSERT_EQ(sink_chain.received().size(), samples.size());
    ASSERT_EQ(sink_fused.received().size(), samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        EXPECT_FLOAT_EQ(sink_chain.received()[i], sink_fused.received()[i]);
    }
}

TEST(FusedBlockTest, SingleBlockChainMatchesDirectKernel) {
    static constexpr size_t kBufferSize = 4096;
    auto samples = make_random_buffer(500);

    GainBlock<float> underlying("Underlying", 3.0f, kBufferSize);
    FusedBlock<GainBlock<float>> fused("FusedSingle", &underlying, kBufferSize);

    static_assert(std::is_same_v<FusedBlock<GainBlock<float>>::FirstIn, float>);
    static_assert(std::is_same_v<FusedBlock<GainBlock<float>>::LastOut, float>);

    VectorSource source("Source", samples);
    VectorSink sink("Sink", kBufferSize);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fused.in),
        cler::BlockRunner(&fused, &sink.in),
        cler::BlockRunner(&sink)
    );
    fg.run();
    while (sink.received().size() < samples.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fg.stop();

    ASSERT_EQ(sink.received().size(), samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        EXPECT_FLOAT_EQ(sink.received()[i], samples[i] * 3.0f);
    }
}
