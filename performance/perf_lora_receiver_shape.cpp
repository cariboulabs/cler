#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/channelizers/polyphase_channelizer.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"

#include <iostream>
#include <vector>
#include <complex>
#include <atomic>
#include <thread>
#include <cstring>
#include <type_traits>

constexpr size_t NUM_CHANNELS = 5;
constexpr size_t CHANNELIZER_IN_BUF = 81920;
constexpr auto TEST_DURATION = std::chrono::seconds(3);

struct FakePlutoSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    explicit FakePlutoSourceBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        std::this_thread::sleep_for(std::chrono::microseconds(REFILL_US));

        auto [write_ptr, write_size] = out->write_dbf();
        size_t n = std::min(write_size, BURST_SIZE);
        if (n == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < n; ++i) {
            write_ptr[i] = std::complex<float>(1.0f, 0.0f);
        }
        out->commit_write(n);

        return cler::Empty{};
    }

    static constexpr size_t REFILL_US = 1000;
    static constexpr size_t SOURCE_RATE_SPS = 4'000'000;
    static constexpr size_t BURST_SIZE = SOURCE_RATE_SPS * REFILL_US / 1'000'000;
};

struct BusyDecoderBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    std::atomic<bool> burst{false};

    explicit BusyDecoderBlock(const char* name)
        : cler::BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        size_t n = std::min(read_size, write_size);
        if (n == 0) {
            return cler::Error::NotEnoughSamples;
        }

        if (burst.load(std::memory_order_relaxed)) {
            const std::complex<float> rotator(0.999f, 0.001f);
            for (size_t i = 0; i < n; ++i) {
                std::complex<float> acc = read_ptr[i];
                for (size_t k = 0; k < BUSY_ITERATIONS; ++k) {
                    acc = acc * rotator + read_ptr[i];
                }
                write_ptr[i] = acc;
            }
        } else {
            std::memcpy(write_ptr, read_ptr, n * sizeof(std::complex<float>));
        }

        in.commit_read(n);
        out->commit_write(n);

        return cler::Empty{};
    }

    static constexpr size_t BUSY_ITERATIONS = 30;
};

size_t count_and_drain(cler::Channel<std::complex<float>>* ch, void* context) {
    size_t n = ch->size();
    static_cast<std::atomic<size_t>*>(context)->fetch_add(n, std::memory_order_relaxed);
    return n;
}

template <typename T, typename = void>
struct has_repartition_check_ms : std::false_type {};
template <typename T>
struct has_repartition_check_ms<T, std::void_t<decltype(std::declval<T&>().repartition_check_ms)>> : std::true_type {};

struct TestResult {
    std::string name;
    double throughput;
    double duration;
    size_t samples;
    bool has_repartitions = false;
    size_t repartitions = 0;

    void print() const {
        std::cout << "=== " << name << " ===" << std::endl;
        std::cout << "  Samples: " << samples << std::endl;
        std::cout << "  Duration: " << duration << " seconds" << std::endl;
        std::cout << "  Throughput: " << throughput << " samples/sec" << std::endl;
        std::cout << "  Performance: " << (throughput / 1e6) << " MSamples/sec" << std::endl;
        if (has_repartitions) {
            std::cout << "  Repartitions: " << repartitions << std::endl;
        }
        std::cout << std::endl;
    }
};

TestResult run_test(const std::string& name, cler::FlowGraphConfig config, bool burst_schedule) {
    std::cout << "Running " << name << " test..." << std::flush;

    std::atomic<size_t> total_samples{0};

    FakePlutoSourceBlock source("Source");
    MultiStageResamplerBlock<std::complex<float>> resampler("Resampler", 1.5f, 60.0f, 16384);
    PolyphaseChannelizerBlock<NUM_CHANNELS, 3> channelizer("Channelizer", 80.0f, CHANNELIZER_IN_BUF);

    BusyDecoderBlock decoder0("Decoder0");
    BusyDecoderBlock decoder1("Decoder1");
    BusyDecoderBlock decoder2("Decoder2");
    BusyDecoderBlock decoder3("Decoder3");
    BusyDecoderBlock decoder4("Decoder4");
    BusyDecoderBlock* decoders[NUM_CHANNELS] = {&decoder0, &decoder1, &decoder2, &decoder3, &decoder4};

    SinkNullBlock<std::complex<float>> sink0("Sink0", count_and_drain, &total_samples);
    SinkNullBlock<std::complex<float>> sink1("Sink1", count_and_drain, &total_samples);
    SinkNullBlock<std::complex<float>> sink2("Sink2", count_and_drain, &total_samples);
    SinkNullBlock<std::complex<float>> sink3("Sink3", count_and_drain, &total_samples);
    SinkNullBlock<std::complex<float>> sink4("Sink4", count_and_drain, &total_samples);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &resampler.in),
        cler::BlockRunner(&resampler, &channelizer.in),
        cler::BlockRunner(&channelizer,
            &decoder0.in, &decoder1.in, &decoder2.in, &decoder3.in, &decoder4.in),
        cler::BlockRunner(&decoder0, &sink0.in),
        cler::BlockRunner(&decoder1, &sink1.in),
        cler::BlockRunner(&decoder2, &sink2.in),
        cler::BlockRunner(&decoder3, &sink3.in),
        cler::BlockRunner(&decoder4, &sink4.in),
        cler::BlockRunner(&sink0),
        cler::BlockRunner(&sink1),
        cler::BlockRunner(&sink2),
        cler::BlockRunner(&sink3),
        cler::BlockRunner(&sink4)
    );

    auto start = std::chrono::steady_clock::now();

    if (burst_schedule) {
        fg.run(config);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        decoders[0]->burst.store(true, std::memory_order_relaxed);
        decoders[1]->burst.store(true, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        decoders[0]->burst.store(false, std::memory_order_relaxed);
        decoders[1]->burst.store(false, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        fg.stop();
    } else {
        fg.run_for(TEST_DURATION, config);
    }

    double duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    TestResult result;
    result.name = name;
    result.samples = total_samples.load();
    result.duration = duration;
    result.throughput = result.samples / duration;
    if (config.scheduler == cler::SchedulerType::PinnedIslands) {
        result.has_repartitions = true;
        result.repartitions = fg.repartition_count();
    }

    std::cout << " DONE" << std::endl;
    return result;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Cler LoRa Ground Station Receiver Shape Performance Test" << std::endl;
    std::cout << "Pipeline: FakePluto -> Resampler(1.5x) -> Channelizer(5ch) -> 5x[BusyDecoder -> SinkNull]" << std::endl;
    std::cout << "Test Duration: " << TEST_DURATION.count() << " seconds per test" << std::endl;
    std::cout << "========================================" << std::endl;

    std::vector<TestResult> results;

    results.push_back(run_test("ThreadPerBlock (default)", cler::FlowGraphConfig{}, false));

    cler::FlowGraphConfig ftp_config;
    ftp_config.scheduler = cler::SchedulerType::FixedThreadPool;
    ftp_config.num_workers = 2;
    results.push_back(run_test("FixedThreadPool (2 workers)", ftp_config, false));

    results.push_back(run_test("embedded_optimized() [PinnedIslands, 2 workers]",
                               cler::flowgraph_config::embedded_optimized(), false));
    results.push_back(run_test("PinnedIslands (1 worker)", cler::flowgraph_config::pinned_islands(1), false));
    results.push_back(run_test("PinnedIslands (2 workers)", cler::flowgraph_config::pinned_islands(2), false));

    auto burst_config = cler::flowgraph_config::pinned_islands(2);
    if constexpr (has_repartition_check_ms<cler::FlowGraphConfig>::value) {
        burst_config.repartition_check_ms = 250;
        std::cout << "repartition_check_ms field found in FlowGraphConfig; using 250ms for burst test" << std::endl;
    } else {
        std::cout << "repartition_check_ms field not found in FlowGraphConfig; running burst test without it" << std::endl;
    }
    results.push_back(run_test("PinnedIslands (2 workers) mid-run burst", burst_config, true));

    std::cout << "========================================" << std::endl;
    std::cout << "Results" << std::endl;
    std::cout << "========================================" << std::endl;

    for (const auto& r : results) {
        r.print();
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Summary: Total Channelizer-Output Throughput" << std::endl;
    std::cout << "========================================" << std::endl;

    printf("%-45s | %14s | %12s\n", "Configuration", "Throughput", "Repartitions");
    printf("%s\n", std::string(78, '-').c_str());
    for (const auto& r : results) {
        if (r.has_repartitions) {
            printf("%-45s | %11.2f MS/s | %12zu\n", r.name.c_str(), r.throughput / 1e6, r.repartitions);
        } else {
            printf("%-45s | %11.2f MS/s | %12s\n", r.name.c_str(), r.throughput / 1e6, "-");
        }
    }
    std::cout << "========================================" << std::endl;

    return 0;
}
