#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

// Include desktop blocks
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/math/gain.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/noise/awgn.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"

#include <iostream>
#include <vector>

// Fanout-focused performance test to showcase different scheduler strengths:
// 1. Uniform fanout (all paths same complexity) -> FixedThreadPool should excel
// 2. Imbalanced fanout (different path complexity) -> AdaptiveLoadBalancing should excel  
// 3. Heavy fanout (many parallel paths) -> Load balancing should excel

constexpr size_t BUFFER_SIZE = 1024;

struct TestResult {
    std::string name;
    double throughput;
    double duration;
    size_t samples;
    double cpu_efficiency;  // successful procedures / total procedures
    
    void print() const {
        std::cout << "=== " << name << " ===" << std::endl;
        std::cout << "  Samples: " << samples << std::endl;
        std::cout << "  Duration: " << duration << " seconds" << std::endl;
        std::cout << "  Throughput: " << throughput << " samples/sec" << std::endl;
        std::cout << "  Performance: " << (throughput / 1e6) << " MSamples/sec" << std::endl;
        std::cout << "  CPU Efficiency: " << (cpu_efficiency * 100.0) << "%" << std::endl;
        std::cout << std::endl;
    }
};

// Sample counter for tracking throughput
struct SampleCounter {
    size_t count = 0;
    std::chrono::steady_clock::time_point start_time;
    
    SampleCounter() {
        start_time = std::chrono::steady_clock::now();
    }
    
    double get_throughput() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - start_time).count();
        return count / elapsed;
    }
};

// Callbacks for sink to count samples
SampleCounter sample_counter;

size_t count_samples_float(cler::Channel<float>* ch, void* context) {
    size_t available = ch->size();
    sample_counter.count += available;
    return available;
}

size_t count_samples_complex(cler::Channel<std::complex<float>>* ch, void* context) {
    size_t available = ch->size();
    sample_counter.count += available;
    return available;
}

TestResult run_baseline_test(std::chrono::seconds test_duration) {
    std::cout << "Running BASELINE test..." << std::flush;
    
    // Reset counter
    sample_counter = SampleCounter();
    
    // Fanout with UNIFORM workload (all paths have same complexity):
    // Source -> Fanout -> [Path1: Light gain -> Sink, Path2: Light gain -> Sink, Path3: Light gain -> Sink]
    // This should favor FixedThreadPool since load is balanced
    
    SourceCWBlock<std::complex<float>> source("CW_Source", 1.0f, 1000.0f, 48000);
    FanoutBlock<std::complex<float>> fanout("Fanout_3way", 3);
    
    // Path 1: Light processing
    GainBlock<std::complex<float>> gain1("Gain1", std::complex<float>(0.8f, 0.0f));
    SinkNullBlock<std::complex<float>> sink1("Sink1", count_samples_complex, &sample_counter);
    
    // Path 2: Light processing (same as path 1)
    GainBlock<std::complex<float>> gain2("Gain2", std::complex<float>(0.9f, 0.0f));
    SinkNullBlock<std::complex<float>> sink2("Sink2", count_samples_complex, &sample_counter);
    
    // Path 3: Light processing (same as path 1)
    GainBlock<std::complex<float>> gain3("Gain3", std::complex<float>(1.1f, 0.0f));
    SinkNullBlock<std::complex<float>> sink3("Sink3", count_samples_complex, &sample_counter);
    
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &gain1.in, &gain2.in, &gain3.in),
        
        // Path 1
        cler::BlockRunner(&gain1, &sink1.in),
        cler::BlockRunner(&sink1),
        
        // Path 2  
        cler::BlockRunner(&gain2, &sink2.in),
        cler::BlockRunner(&sink2),
        
        // Path 3
        cler::BlockRunner(&gain3, &sink3.in),
        cler::BlockRunner(&sink3)
    );

    // Run for specified duration
    fg.run_for(test_duration);
    
    double duration = test_duration.count();
    
    // Calculate CPU efficiency from stats
    const auto& stats = fg.stats();
    size_t total_successful = 0;
    size_t total_procedures = 0;
    for (const auto& stat : stats) {
        total_successful += stat.successful_procedures;
        total_procedures += stat.successful_procedures + stat.failed_procedures;
    }
    double cpu_efficiency = total_procedures > 0 ? double(total_successful) / total_procedures : 0.0;
    
    std::cout << " DONE" << std::endl;
    
    return {
        "BASELINE: ThreadPerBlock (no features)",
        sample_counter.get_throughput(),
        duration,
        sample_counter.count,
        cpu_efficiency
    };
}

TestResult run_enhanced_test(const std::string& name, cler::FlowGraphConfig config, std::chrono::seconds test_duration) {
    std::cout << "Running " << name << " test..." << std::flush;
    
    // Reset counter
    sample_counter = SampleCounter();
    
    // Same uniform fanout workload as baseline
    SourceCWBlock<std::complex<float>> source("CW_Source", 1.0f, 1000.0f, 48000);
    FanoutBlock<std::complex<float>> fanout("Fanout_3way", 3);
    
    // Path 1: Light processing
    GainBlock<std::complex<float>> gain1("Gain1", std::complex<float>(0.8f, 0.0f));
    SinkNullBlock<std::complex<float>> sink1("Sink1", count_samples_complex, &sample_counter);
    
    // Path 2: Light processing (same as path 1)
    GainBlock<std::complex<float>> gain2("Gain2", std::complex<float>(0.9f, 0.0f));
    SinkNullBlock<std::complex<float>> sink2("Sink2", count_samples_complex, &sample_counter);
    
    // Path 3: Light processing (same as path 1)
    GainBlock<std::complex<float>> gain3("Gain3", std::complex<float>(1.1f, 0.0f));
    SinkNullBlock<std::complex<float>> sink3("Sink3", count_samples_complex, &sample_counter);
    
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &gain1.in, &gain2.in, &gain3.in),
        
        // Path 1
        cler::BlockRunner(&gain1, &sink1.in),
        cler::BlockRunner(&sink1),
        
        // Path 2  
        cler::BlockRunner(&gain2, &sink2.in),
        cler::BlockRunner(&sink2),
        
        // Path 3
        cler::BlockRunner(&gain3, &sink3.in),
        cler::BlockRunner(&sink3)
    );

    // Run for specified duration
    fg.run_for(test_duration, config);
    
    double duration = test_duration.count();
    
    // Calculate CPU efficiency from stats
    const auto& stats = fg.stats();
    size_t total_successful = 0;
    size_t total_procedures = 0;
    for (const auto& stat : stats) {
        total_successful += stat.successful_procedures;
        total_procedures += stat.successful_procedures + stat.failed_procedures;
    }
    double cpu_efficiency = total_procedures > 0 ? double(total_successful) / total_procedures : 0.0;
    
    std::cout << " DONE" << std::endl;
    
    return {
        name,
        sample_counter.get_throughput(),
        duration,
        sample_counter.count,
        cpu_efficiency
    };
}

TestResult run_imbalanced_test(const std::string& name, cler::FlowGraphConfig config, std::chrono::seconds test_duration) {
    std::cout << "Running " << name << " (IMBALANCED FANOUT) test..." << std::flush;
    
    // Reset counter
    sample_counter = SampleCounter();
    
    // Fanout with IMBALANCED workload (different path complexity):
    // Source -> Fanout -> [Path1: Light gain -> Sink, Path2: Heavy noise+gain -> Sink, Path3: Very light -> Sink]
    // This should favor AdaptiveLoadBalancing since it can rebalance heavy path
    
    SourceCWBlock<std::complex<float>> source("CW_Source", 1.0f, 1000.0f, 48000);
    FanoutBlock<std::complex<float>> fanout("Fanout_3way", 3);
    
    // Path 1: Light processing
    GainBlock<std::complex<float>> gain1("LightGain", std::complex<float>(0.8f, 0.0f));
    SinkNullBlock<std::complex<float>> sink1("Sink1", count_samples_complex, &sample_counter);
    
    // Path 2: HEAVY processing (noise + gain = more CPU)
    NoiseAWGNBlock<std::complex<float>> noise2("HeavyNoise", 0.1f);
    GainBlock<std::complex<float>> gain2("HeavyGain", std::complex<float>(0.9f, 0.0f));
    SinkNullBlock<std::complex<float>> sink2("Sink2", count_samples_complex, &sample_counter);
    
    // Path 3: Very light processing (just sink)
    SinkNullBlock<std::complex<float>> sink3("Sink3", count_samples_complex, &sample_counter);
    
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &gain1.in, &noise2.in, &sink3.in),
        
        // Path 1: Light
        cler::BlockRunner(&gain1, &sink1.in),
        cler::BlockRunner(&sink1),
        
        // Path 2: Heavy  
        cler::BlockRunner(&noise2, &gain2.in),
        cler::BlockRunner(&gain2, &sink2.in),
        cler::BlockRunner(&sink2),
        
        // Path 3: Very light (direct sink)
        cler::BlockRunner(&sink3)
    );

    // Run for specified duration
    fg.run_for(test_duration, config);
    
    double duration = test_duration.count();
    
    // Calculate CPU efficiency from stats
    const auto& stats = fg.stats();
    size_t total_successful = 0;
    size_t total_procedures = 0;
    for (const auto& stat : stats) {
        total_successful += stat.successful_procedures;
        total_procedures += stat.successful_procedures + stat.failed_procedures;
    }
    double cpu_efficiency = total_procedures > 0 ? double(total_successful) / total_procedures : 0.0;
    
    std::cout << " DONE" << std::endl;
    
    return {
        name + " [IMBALANCED]",
        sample_counter.get_throughput(),
        duration,
        sample_counter.count,
        cpu_efficiency
    };
}

TestResult run_heavy_fanout_test(const std::string& name, cler::FlowGraphConfig config, std::chrono::seconds test_duration) {
    std::cout << "Running " << name << " (HEAVY FANOUT) test..." << std::flush;
    
    // Reset counter
    sample_counter = SampleCounter();
    
    // Fanout with MANY parallel paths (8-way fanout):
    // Source -> Fanout -> [8 paths: Gain->Sink, Gain->Sink, ...]
    // This tests scheduler ability to handle many parallel blocks efficiently
    // Load balancing should excel at distributing many blocks across workers
    
    SourceCWBlock<std::complex<float>> source("CW_Source", 1.0f, 1000.0f, 48000);
    FanoutBlock<std::complex<float>> fanout("Fanout_8way", 8);
    
    // Create 8 parallel paths, each with gain + sink
    GainBlock<std::complex<float>> gain1("Gain1", std::complex<float>(0.8f, 0.0f));
    GainBlock<std::complex<float>> gain2("Gain2", std::complex<float>(0.9f, 0.0f));
    GainBlock<std::complex<float>> gain3("Gain3", std::complex<float>(1.0f, 0.0f));
    GainBlock<std::complex<float>> gain4("Gain4", std::complex<float>(1.1f, 0.0f));
    GainBlock<std::complex<float>> gain5("Gain5", std::complex<float>(0.7f, 0.0f));
    GainBlock<std::complex<float>> gain6("Gain6", std::complex<float>(1.2f, 0.0f));
    GainBlock<std::complex<float>> gain7("Gain7", std::complex<float>(0.6f, 0.0f));
    GainBlock<std::complex<float>> gain8("Gain8", std::complex<float>(1.3f, 0.0f));
    
    SinkNullBlock<std::complex<float>> sink1("Sink1", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink2("Sink2", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink3("Sink3", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink4("Sink4", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink5("Sink5", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink6("Sink6", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink7("Sink7", count_samples_complex, &sample_counter);
    SinkNullBlock<std::complex<float>> sink8("Sink8", count_samples_complex, &sample_counter);
    
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &gain1.in, &gain2.in, &gain3.in, &gain4.in, 
                                  &gain5.in, &gain6.in, &gain7.in, &gain8.in),
        
        // 8 parallel paths
        cler::BlockRunner(&gain1, &sink1.in), cler::BlockRunner(&sink1),
        cler::BlockRunner(&gain2, &sink2.in), cler::BlockRunner(&sink2),
        cler::BlockRunner(&gain3, &sink3.in), cler::BlockRunner(&sink3),
        cler::BlockRunner(&gain4, &sink4.in), cler::BlockRunner(&sink4),
        cler::BlockRunner(&gain5, &sink5.in), cler::BlockRunner(&sink5),
        cler::BlockRunner(&gain6, &sink6.in), cler::BlockRunner(&sink6),
        cler::BlockRunner(&gain7, &sink7.in), cler::BlockRunner(&sink7),
        cler::BlockRunner(&gain8, &sink8.in), cler::BlockRunner(&sink8)
    );

    // Run for specified duration
    fg.run_for(test_duration, config);
    
    double duration = test_duration.count();
    
    // Calculate CPU efficiency from stats
    const auto& stats = fg.stats();
    size_t total_successful = 0;
    size_t total_procedures = 0;
    for (const auto& stat : stats) {
        total_successful += stat.successful_procedures;
        total_procedures += stat.successful_procedures + stat.failed_procedures;
    }
    double cpu_efficiency = total_procedures > 0 ? double(total_successful) / total_procedures : 0.0;
    
    std::cout << " DONE" << std::endl;
    
    return {
        name + " [HEAVY FANOUT]",
        sample_counter.get_throughput(),
        duration,
        sample_counter.count,
        cpu_efficiency
    };
}

int main() {
    // Test duration: 3 seconds for each test 
    const auto test_duration = std::chrono::seconds(3);
    
    std::cout << "========================================" << std::endl;
    std::cout << "Cler Fanout Workload Performance Test" << std::endl;
    std::cout << "Testing scheduler performance on fanout scenarios:" << std::endl;
    std::cout << "1. UNIFORM fanout (all paths same complexity) -> FixedThreadPool should excel" << std::endl;
    std::cout << "2. IMBALANCED fanout (different path complexity) -> AdaptiveLoadBalancing should excel" << std::endl;
    std::cout << "3. HEAVY fanout (many parallel paths) -> Load balancing should excel" << std::endl;
    std::cout << "BASELINE: ThreadPerBlock scheduler with no feature extensions" << std::endl;
    std::cout << "Test Duration: " << test_duration.count() << " seconds per test" << std::endl;
    std::cout << "Metrics: Throughput + CPU Efficiency (successful/total procedures)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    TestResult baseline_result;
    
    std::cout << "\n🔄 UNIFORM FANOUT TESTS (3 equal paths):" << std::endl;
    std::cout << "Pipeline: Source -> Fanout -> [Gain->Sink, Gain->Sink, Gain->Sink] (8 blocks)" << std::endl;
    std::cout << "Expected: FixedThreadPool should perform best due to balanced load" << std::endl;
    
    // Test 1: Baseline ThreadPerBlock (uniform) - Store for comparisons
    baseline_result = run_baseline_test(test_duration);
    results.push_back(baseline_result);
    
    // Test 2: FixedThreadPool with 4 workers (should excel at uniform)
    auto fixed_config = cler::flowgraph_config::desktop_performance();
    results.push_back(run_enhanced_test("FixedThreadPool (4 workers)", fixed_config, test_duration));
    
    // Test 3: AdaptiveLoadBalancing (should be decent but not optimal for uniform)
    auto loadbalance_config = cler::flowgraph_config::adaptive_load_balancing();
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    std::cout << "\n⚖️ IMBALANCED FANOUT TESTS (light/heavy/very-light paths):" << std::endl;
    std::cout << "Pipeline: Source -> Fanout -> [Gain->Sink, Noise+Gain->Sink, DirectSink] (8 blocks)" << std::endl;
    std::cout << "Expected: AdaptiveLoadBalancing should perform best due to imbalanced load" << std::endl;
    
    // Test 4: FixedThreadPool (should struggle with imbalanced load)
    results.push_back(run_imbalanced_test("FixedThreadPool (4 workers)", fixed_config, test_duration));
    
    // Test 5: AdaptiveLoadBalancing (should excel at imbalanced)
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    // Test 6: Aggressive AdaptiveLoadBalancing (should be even better)
    auto aggressive_config = cler::flowgraph_config::adaptive_load_balancing();
    aggressive_config.load_balancing_interval = 100;   // Very frequent rebalancing
    aggressive_config.load_balancing_threshold = 0.05; // Very sensitive (5% imbalance)
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing (aggressive)", aggressive_config, test_duration));
    
    std::cout << "\n🚀 HEAVY FANOUT TESTS (8 parallel paths):" << std::endl;
    std::cout << "Pipeline: Source -> Fanout -> [8x Gain->Sink paths] (18 blocks total)" << std::endl;
    std::cout << "Expected: Load balancing should excel with many blocks to distribute" << std::endl;
    
    // Test 7: FixedThreadPool (should handle many blocks reasonably)
    results.push_back(run_heavy_fanout_test("FixedThreadPool (4 workers)", fixed_config, test_duration));
    
    // Test 8: AdaptiveLoadBalancing (should excel with many blocks to balance)
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    // Test 9: Aggressive AdaptiveLoadBalancing (should be excellent)
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing (aggressive)", aggressive_config, test_duration));

    // Print results
    std::cout << "========================================" << std::endl;
    std::cout << "Fanout Workload Performance Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // Analysis - All comparisons vs ThreadPerBlock baseline
    std::cout << "========================================" << std::endl;
    std::cout << "Performance Analysis vs BASELINE (ThreadPerBlock)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (results.size() >= 3) {
        // Use baseline for all comparisons
        double baseline_throughput = baseline_result.throughput;
        
        std::cout << "🔄 UNIFORM FANOUT Analysis:" << std::endl;
        std::cout << "  BASELINE (ThreadPerBlock): " << (baseline_throughput/1e6) << " MSamples/sec" << std::endl;
        std::cout << "  FixedThreadPool: " << (results[1].throughput/1e6) << " MSamples/sec ";
        std::cout << "(" << std::showpos << ((results[1].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        std::cout << "  AdaptiveLoadBalancing: " << (results[2].throughput/1e6) << " MSamples/sec ";
        std::cout << "(" << std::showpos << ((results[2].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        
        if (results.size() >= 6) {
            std::cout << "\n⚖️ IMBALANCED FANOUT Analysis:" << std::endl;
            std::cout << "  FixedThreadPool (imbalanced): " << (results[3].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[3].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
            std::cout << "  AdaptiveLoadBalancing (imbalanced): " << (results[4].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[4].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
            std::cout << "  AdaptiveLoadBalancing (aggressive): " << (results[5].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[5].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        }
        
        if (results.size() >= 9) {
            std::cout << "\n🚀 HEAVY FANOUT Analysis:" << std::endl;
            std::cout << "  FixedThreadPool (heavy): " << (results[6].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[6].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
            std::cout << "  AdaptiveLoadBalancing (heavy): " << (results[7].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[7].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
            std::cout << "  AdaptiveLoadBalancing (aggressive heavy): " << (results[8].throughput/1e6) << " MSamples/sec ";
            std::cout << "(" << std::showpos << ((results[8].throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        }
        
        // Find best for each scenario
        auto uniform_best = std::max_element(results.begin(), results.begin() + 3, 
            [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
        std::cout << "\n🏆 Best for UNIFORM fanout: " << uniform_best->name;
        if (uniform_best->throughput != baseline_throughput) {
            std::cout << " (" << std::showpos << ((uniform_best->throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)";
        }
        std::cout << std::endl;
        
        if (results.size() >= 6) {
            auto imbalanced_best = std::max_element(results.begin() + 3, results.begin() + 6, 
                [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
            std::cout << "🏆 Best for IMBALANCED fanout: " << imbalanced_best->name;
            std::cout << " (" << std::showpos << ((imbalanced_best->throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        }
        
        if (results.size() >= 9) {
            auto heavy_best = std::max_element(results.begin() + 6, results.end(), 
                [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
            std::cout << "🏆 Best for HEAVY fanout: " << heavy_best->name;
            std::cout << " (" << std::showpos << ((heavy_best->throughput - baseline_throughput) / baseline_throughput) * 100.0 << "% vs baseline)" << std::endl;
        }
    }
    
    std::cout << "========================================" << std::endl;
    
    return 0;
}