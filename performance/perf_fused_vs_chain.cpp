#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/gain.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include "desktop_blocks/utils/fused.hpp"
#include <iostream>
#include <cstring>

constexpr size_t BUFFER_SIZE = 4096;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(std::string name)
        : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + BUFFER_SIZE, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        size_t to_write = std::min(write_size, BUFFER_SIZE);

        if (to_write == 0) {
            return cler::Error::NotEnoughSpace;
        }

        std::memcpy(write_ptr, _buffer, to_write * sizeof(float));
        out->commit_write(to_write);

        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(std::string name, size_t buffer_size)
        : BlockBase(std::move(name)), in(buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_read = std::min(in.size(), BUFFER_SIZE);
        if (to_read == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.commit_read(to_read);
        _received += to_read;
        return cler::Empty{};
    }

    size_t received() const { return _received; }

private:
    size_t _received = 0;
};

struct TestResult {
    std::string name;
    double throughput;
    size_t samples;

    void print() const {
        std::cout << "=== " << name << " ===" << std::endl;
        std::cout << "  Samples: " << samples << std::endl;
        std::cout << "  Throughput: " << throughput << " samples/sec ("
                   << (throughput / 1e6) << " MSamples/sec)" << std::endl;
        std::cout << std::endl;
    }
};

TestResult run_chain_test(std::chrono::seconds duration) {
    std::cout << "Running SEPARATE BLOCKS test..." << std::flush;

    SourceBlock source("Source");
    GainBlock<float> gain0("Gain0", 1.5f, BUFFER_SIZE);
    GainBlock<float> gain1("Gain1", 0.8f, BUFFER_SIZE);
    GainBlock<float> gain2("Gain2", 1.2f, BUFFER_SIZE);
    SinkBlock sink("Sink", BUFFER_SIZE);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain0.in),
        cler::BlockRunner(&gain0, &gain1.in),
        cler::BlockRunner(&gain1, &gain2.in),
        cler::BlockRunner(&gain2, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    fg.run_for(duration, config);

    std::cout << " DONE" << std::endl;

    return {"Separate blocks: Source -> Gain -> Gain -> Gain -> Sink (PinnedIslands, 2 workers)",
            static_cast<double>(sink.received()) / duration.count(), sink.received()};
}

TestResult run_fused_test(std::chrono::seconds duration) {
    std::cout << "Running FUSED BLOCK test..." << std::flush;

    SourceBlock source("Source");
    FusedBlock<GainKernel<float>, GainKernel<float>, GainKernel<float>> fused(
        "FusedGains", GainKernel<float>{1.5f}, GainKernel<float>{0.8f}, GainKernel<float>{1.2f}, BUFFER_SIZE);
    SinkBlock sink("Sink", BUFFER_SIZE);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fused.in),
        cler::BlockRunner(&fused, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto config = cler::flowgraph_config::pinned_islands(2);
    fg.run_for(duration, config);

    std::cout << " DONE" << std::endl;

    return {"FusedBlock: Source -> FusedBlock<Gain,Gain,Gain> -> Sink (PinnedIslands, 2 workers)",
            static_cast<double>(sink.received()) / duration.count(), sink.received()};
}

int main() {
    const auto test_duration = std::chrono::seconds(3);

    std::cout << "========================================" << std::endl;
    std::cout << "Cler FusedBlock vs Separate Blocks Performance Test" << std::endl;
    std::cout << "Pipeline: Source -> Gain -> Gain -> Gain -> Sink" << std::endl;
    std::cout << "Scheduler: PinnedIslands, 2 workers, both cases" << std::endl;
    std::cout << "Test Duration: " << test_duration.count() << " seconds per test" << std::endl;
    std::cout << "========================================" << std::endl;

    TestResult chain_result = run_chain_test(test_duration);
    TestResult fused_result = run_fused_test(test_duration);

    std::cout << "========================================" << std::endl;
    std::cout << "Results" << std::endl;
    std::cout << "========================================" << std::endl;

    chain_result.print();
    fused_result.print();

    double speedup = fused_result.throughput / chain_result.throughput;
    std::cout << "FusedBlock speedup vs separate blocks: " << speedup << "x" << std::endl;

    return 0;
}
