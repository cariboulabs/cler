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
    
    size_t get_samples_processed() const {
        return _received;
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

TestResult run_baseline_test(std::chrono::seconds test_duration) {
    std::cout << "Running BASELINE test..." << std::flush;
    
    SourceBlock source("Source");
    CopyBlock stage0("Stage0");
    CopyBlock stage1("Stage1");
    CopyBlock stage2("Stage2");
    CopyBlock stage3("Stage3");
    SinkBlock sink("Sink", SIZE_MAX);  // No sample limit

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &sink.in),
        cler::BlockRunner(&sink)
    );

    // Run for specified duration
    fg.run_for(test_duration);
    
    double duration = test_duration.count();
    
    std::cout << " DONE" << std::endl;
    
    return {
        "Baseline (ThreadPerBlock)",
        sink.get_throughput(),
        duration,
        sink.get_samples_processed()  // Access the actual samples processed
    };
}

TestResult run_enhanced_test(const std::string& name, cler::FlowGraphConfig config, std::chrono::seconds test_duration) {
    std::cout << "Running " << name << " test..." << std::flush;
    
    SourceBlock source("Source");
    CopyBlock stage0("Stage0");
    CopyBlock stage1("Stage1");
    CopyBlock stage2("Stage2");
    CopyBlock stage3("Stage3");
    SinkBlock sink("Sink", SIZE_MAX);  // No sample limit

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &sink.in),
        cler::BlockRunner(&sink)
    );

    // Run for specified duration
    fg.run_for(test_duration, config);
    
    double duration = test_duration.count();
    
    std::cout << " DONE" << std::endl;
    
    return {
        name,
        sink.get_throughput(),
        duration,
        sink.get_samples_processed()
    };
}

int main() {
    // Test duration: 5 seconds for each test for more robust results
    const auto test_duration = std::chrono::seconds(5);
    
    std::cout << "========================================" << std::endl;
    std::cout << "Cler Performance Features Test" << std::endl;
    std::cout << "Pipeline: Source -> 4x Copy -> Sink" << std::endl;
    std::cout << "Test Duration: " << test_duration.count() << " seconds per test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Test 1: Baseline ThreadPerBlock
    results.push_back(run_baseline_test(test_duration));
    
    // Test 2: FixedThreadPool with 2 workers (embedded-style)
    auto conservative_config = cler::flowgraph_config::embedded_optimized();
    results.push_back(run_enhanced_test("FixedThreadPool (2 workers)", conservative_config, test_duration));
    
    // Test 3: FixedThreadPool with 4 workers (desktop-style)
    auto default_config = cler::flowgraph_config::desktop_performance();
    results.push_back(run_enhanced_test("FixedThreadPool (4 workers)", default_config, test_duration));
    
    // Test 4: AdaptiveLoadBalancing with default rebalancing
    auto loadbalance_config = cler::flowgraph_config::adaptive_load_balancing();
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing (default)", loadbalance_config, test_duration));
    
    // Test 5: AdaptiveLoadBalancing with aggressive rebalancing
    auto aggressive_config = cler::flowgraph_config::adaptive_load_balancing();
    aggressive_config.load_balancing_interval = 200;   // More frequent rebalancing
    aggressive_config.load_balancing_threshold = 0.1; // Lower threshold for rebalancing
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing (aggressive)", aggressive_config, test_duration));
    
    // Test 6: ThreadPerBlock with conservative adaptive sleep (rarely sleeps)
    auto conservative_sleep_config = cler::flowgraph_config::thread_per_block_adaptive_sleep();
    conservative_sleep_config.adaptive_sleep_max_us = 1000.0; // Lower max sleep time
    conservative_sleep_config.adaptive_sleep_multiplier = 2.0; // High growth
    conservative_sleep_config.adaptive_sleep_fail_threshold = 20; // More fails before sleeping
    results.push_back(run_enhanced_test("ThreadPerBlock (conservative adaptive sleep)", conservative_sleep_config, test_duration));

    // Test 7: ThreadPerBlock with adaptive sleep (for sparse data)
    auto adaptive_sleep_config = cler::flowgraph_config::thread_per_block_adaptive_sleep();
    results.push_back(run_enhanced_test("ThreadPerBlock (default adaptive sleep)", adaptive_sleep_config, test_duration));
    
    // Test 8: ThreadPerBlock with aggressive adaptive sleep (for very sparse data)
    auto aggressive_sleep_config = cler::flowgraph_config::thread_per_block_adaptive_sleep();
    aggressive_sleep_config.adaptive_sleep_multiplier = 2.0; // Aggressive growth
    aggressive_sleep_config.adaptive_sleep_fail_threshold = 5; // Fewer fails before sleeping
    aggressive_sleep_config.adaptive_sleep_max_us = 10000.0; // Higher max
    results.push_back(run_enhanced_test("ThreadPerBlock (aggressive adaptive sleep)", aggressive_sleep_config, test_duration));
    
    // Test 9: FixedThreadPool with adaptive sleep (NEW - now possible!)
    auto fixed_pool_sleep_config = cler::flowgraph_config::desktop_performance();
    fixed_pool_sleep_config.adaptive_sleep = true;
    fixed_pool_sleep_config.adaptive_sleep_multiplier = 1.5;
    fixed_pool_sleep_config.adaptive_sleep_max_us = 5000.0;
    fixed_pool_sleep_config.adaptive_sleep_fail_threshold = 10;
    results.push_back(run_enhanced_test("FixedThreadPool (with adaptive sleep)", fixed_pool_sleep_config, test_duration));
    
    // Test 10: AdaptiveLoadBalancing with adaptive sleep (NEW - best of both worlds!)
    auto loadbalance_sleep_config = cler::flowgraph_config::adaptive_load_balancing();
    loadbalance_sleep_config.adaptive_sleep = true;
    loadbalance_sleep_config.adaptive_sleep_multiplier = 1.5;
    loadbalance_sleep_config.adaptive_sleep_max_us = 5000.0;
    loadbalance_sleep_config.adaptive_sleep_fail_threshold = 10;
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing (with adaptive sleep)", loadbalance_sleep_config, test_duration));

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
        std::cout << "Performance Improvements vs Baseline (thread per block):" << std::endl;
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