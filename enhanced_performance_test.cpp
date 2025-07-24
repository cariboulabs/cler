#include "cler.hpp"
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

TestResult run_legacy_test(size_t samples) {
    std::cout << "Running LEGACY test..." << std::flush;
    
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
        "Legacy (ThreadPerBlock)",
        sink.get_throughput(),
        duration,
        samples
    };
}

TestResult run_enhanced_test(const std::string& name, cler::EnhancedFlowGraphConfig config, size_t samples) {
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
    std::cout << "Enhanced vs Legacy Performance Test" << std::endl;
    std::cout << "Pipeline: Source -> 4x Copy -> Sink" << std::endl;
    std::cout << "Samples: " << SAMPLES << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Test 1: Legacy ThreadPerBlock
    results.push_back(run_legacy_test(SAMPLES));
    
    // Test 2: Enhanced FixedThreadPool (conservative)
    cler::EnhancedFlowGraphConfig conservative_config;
    conservative_config.scheduler = cler::SchedulerType::FixedThreadPool;
    conservative_config.num_workers = 2;
    conservative_config.reduce_error_checks = false;  // Keep safe
    results.push_back(run_enhanced_test("Enhanced (2 workers, safe)", conservative_config, SAMPLES));
    
    // Test 3: Enhanced FixedThreadPool (optimized)
    cler::EnhancedFlowGraphConfig optimized_config;
    optimized_config.scheduler = cler::SchedulerType::FixedThreadPool;
    optimized_config.num_workers = 4;
    optimized_config.reduce_error_checks = true;   // Enable optimizations
    optimized_config.min_work_threshold = 8;       // Batch small work
    results.push_back(run_enhanced_test("Enhanced (4 workers, optimized)", optimized_config, SAMPLES));
    
    // Test 4: Enhanced FixedThreadPool (auto workers)
    cler::EnhancedFlowGraphConfig auto_config;
    auto_config.scheduler = cler::SchedulerType::FixedThreadPool;
    auto_config.num_workers = 0;  // Auto-detect
    auto_config.reduce_error_checks = true;
    auto_config.min_work_threshold = 4;
    results.push_back(run_enhanced_test("Enhanced (auto workers, optimized)", auto_config, SAMPLES));
    
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
        std::cout << "Performance Improvements vs Legacy:" << std::endl;
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