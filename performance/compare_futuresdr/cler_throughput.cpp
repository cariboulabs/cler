#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>

constexpr size_t BUFFER_SIZE = 1024;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(std::string name)
        : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + BUFFER_SIZE, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t to_write = std::min({out->space(), BUFFER_SIZE});
        out->writeN(_buffer, to_write);

        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

struct CopyBlock : public cler::BlockBase {
    cler::Channel<float> in;

    CopyBlock(std::string name)
        : BlockBase(std::move(name)), in(BUFFER_SIZE),
          _rng(std::random_device{}()), _dist(1, 512) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t chunk = _dist(_rng);
        size_t transferable = std::min({in.size(), out->space(), chunk});

        if (transferable == 0) return cler::Error::NotEnoughSamples;

        in.readN(_tmp, transferable);
        out->writeN(_tmp, transferable);

        return cler::Empty{};
    }

private:
    float _tmp[BUFFER_SIZE];
    std::mt19937 _rng;
    std::uniform_int_distribution<size_t> _dist;
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(std::string name, size_t expected)
        : BlockBase(std::move(name)), in(BUFFER_SIZE), _expected_samples(expected) {
        _start_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_read = std::min(in.size(), BUFFER_SIZE);
        if (to_read == 0) {
            return cler::Error::NotEnoughSamples;
        }

        in.commit_read(to_read);
        _received += to_read;

        return cler::Empty{};
    }

    bool is_done() const {
        return _received >= _expected_samples;
    }

    void print_execution() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - _start_time).count();
        std::cout << "Processed " << _received << " samples in "
                  << elapsed << "s → Throughput: "
                  << (_received / elapsed) << " samples/s" << std::endl;
    }

private:
    size_t _received = 0;
    size_t _expected_samples;
    std::chrono::steady_clock::time_point _start_time;
};

int main() {
    constexpr size_t STAGES = 4;
    constexpr size_t SAMPLES = 256'000'000;

    SourceBlock source("Source");
    CopyBlock stage0("Stage0");
    CopyBlock stage1("Stage1");
    CopyBlock stage2("Stage2");
    CopyBlock stage3("Stage3");
    SinkBlock sink("Sink", SAMPLES);

    auto fg = make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &sink.in),
        cler::BlockRunner(&sink)
    );

    fg.run();

    while (!sink.is_done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fg.stop();

    sink.print_execution();
    return 0;
}
