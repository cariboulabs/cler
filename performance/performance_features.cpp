#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>

// Same blocks as original performance test for fair comparison
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
    
    double get_throughput() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - _start_time).count();
        return _received / elapsed;
    }

private:
    size_t _received = 0;
    size_t _expected_samples;
    std::chrono::steady_clock::time_point _start_time;
};

struct TestResult {
    std::string name;
    double throughput;
    double duration;
    size_t samples;
    
    void print() const {
        std::cout << "=== " << name << " ===" << std::endl;
        std::cout << "  Samples: " << samples << std::endl;
        std::cout << "  Duration: " << duration << " seconds" << std::endl;
        std::cout << "  Throughput: " << throughput << " samples/sec" << std::endl;
        std::cout << "  Performance: " << (throughput / 1e6) << " MSamples/sec" << std::endl;
        std::cout << std::endl;
    }
};

TestResult run_baseline_test(size_t samples) {
    std::cout << "Running BASELINE test..." << std::flush;
    
    SourceBlock source("Source");
    CopyBlock stage0("Stage0");
    CopyBlock stage1("Stage1");
    CopyBlock stage2("Stage2");
    CopyBlock stage3("Stage3");
    SinkBlock sink("Sink", samples);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto start = std::chrono::steady_clock::now();
    fg.run();  // Default legacy configuration

    while (!sink.is_done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fg.stop();
    auto end = std::chrono::steady_clock::now();
    
    double duration = std::chrono::duration<double>(end - start).count();
    
    std::cout << " DONE" << std::endl;
    
    return {
        "Baseline (ThreadPerBlock)",
        sink.get_throughput(),
        duration,
        samples
    };
}

TestResult run_enhanced_test(const std::string& name, cler::FlowGraphConfig config, size_t samples) {
    std::cout << "Running " << name << " test..." << std::flush;
    
    SourceBlock source("Source");
    CopyBlock stage0("Stage0");
    CopyBlock stage1("Stage1");
    CopyBlock stage2("Stage2");
    CopyBlock stage3("Stage3");
    SinkBlock sink("Sink", samples);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &sink.in),
        cler::BlockRunner(&sink)
    );

    auto start = std::chrono::steady_clock::now();
    fg.run(config);  // Enhanced configuration

    while (!sink.is_done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fg.stop();
    auto end = std::chrono::steady_clock::now();
    
    double duration = std::chrono::duration<double>(end - start).count();
    
    std::cout << " DONE" << std::endl;
    
    return {
        name,
        sink.get_throughput(),
        duration,
        samples
    };
}

int main() {
    // Same test parameters as original
    constexpr size_t SAMPLES = 256'000'000;
    
    std::cout << "========================================" << std::endl;
    std::cout << "Cler Performance Features Test" << std::endl;
    std::cout << "Pipeline: Source -> 4x Copy -> Sink" << std::endl;
    std::cout << "Samples: " << SAMPLES << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Test 1: Baseline ThreadPerBlock
    results.push_back(run_baseline_test(SAMPLES));
    
    // Test 2: Enhanced FixedThreadPool (conservative)  
    auto conservative_config = cler::flowgraph_config::embedded_optimized();
    results.push_back(run_enhanced_test("Enhanced (2 workers, safe)", conservative_config, SAMPLES));
    
    // Test 3: Enhanced FixedThreadPool (optimized)
    auto optimized_config = cler::flowgraph_config::desktop_performance();
    optimized_config.num_workers = 4;  // Override auto-detect
    optimized_config.min_work_threshold = 8;  // Batch small work
    results.push_back(run_enhanced_test("Enhanced (4 workers, optimized)", optimized_config, SAMPLES));
    
    // Test 4: Enhanced FixedThreadPool (auto workers)
    auto auto_config = cler::flowgraph_config::desktop_performance();
    results.push_back(run_enhanced_test("Enhanced (auto workers, optimized)", auto_config, SAMPLES));
    
    // Test 5: Adaptive Load Balancing (default settings)
    auto loadbalance_config = cler::flowgraph_config::adaptive_load_balancing();
    loadbalance_config.num_workers = 4;
    results.push_back(run_enhanced_test("Adaptive Load Balancing (4 workers)", loadbalance_config, SAMPLES));
    
    // Test 6: Adaptive Load Balancing (aggressive settings)
    auto aggressive_config = cler::flowgraph_config::adaptive_load_balancing();
    aggressive_config.num_workers = 4;
    aggressive_config.rebalance_interval = 200;   // More frequent rebalancing
    aggressive_config.load_balance_threshold = 0.1; // Lower threshold for rebalancing
    results.push_back(run_enhanced_test("Adaptive Load Balancing (aggressive)", aggressive_config, SAMPLES));
    
    // Print results
    std::cout << "========================================" << std::endl;
    std::cout << "Performance Test Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // Calculate improvements vs baseline
    if (results.size() >= 2) {
        double baseline = results[0].throughput;
        std::cout << "Performance Improvements vs Baseline:" << std::endl;
        for (size_t i = 1; i < results.size(); ++i) {
            double improvement = ((results[i].throughput - baseline) / baseline) * 100.0;
            std::cout << "  " << results[i].name << ": " 
                      << std::showpos << improvement << "%" << std::endl;
        }
        std::cout << std::endl;
        
        // Find best result
        auto best = std::max_element(results.begin() + 1, results.end(), 
            [](const TestResult& a, const TestResult& b) {
                return a.throughput < b.throughput;
            });
        
        std::cout << "🏆 Best Enhancement: " << best->name << std::endl;
        std::cout << "🚀 Speed Improvement: " 
                  << std::showpos << ((best->throughput - baseline) / baseline) * 100.0 
                  << "% (" << (best->throughput / baseline) << "x faster)" << std::endl;
    }
    
    std::cout << "========================================" << std::endl;
    
    return 0;
}