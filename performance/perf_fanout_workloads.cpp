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
    std::cout << "ADAPTIVE SLEEP: Tests both with/without adaptive sleep for CPU efficiency" << std::endl;
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
    
    // Test 2b: FixedThreadPool + Adaptive Sleep
    auto fixed_config_sleep = cler::flowgraph_config::desktop_performance();
    fixed_config_sleep.adaptive_sleep = true;
    results.push_back(run_enhanced_test("FixedThreadPool + adaptive sleep", fixed_config_sleep, test_duration));
    
    // Test 3: AdaptiveLoadBalancing (should be decent but not optimal for uniform)
    auto loadbalance_config = cler::flowgraph_config::adaptive_load_balancing();
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    // Test 3b: AdaptiveLoadBalancing + Adaptive Sleep
    auto loadbalance_config_sleep = cler::flowgraph_config::adaptive_load_balancing();
    loadbalance_config_sleep.adaptive_sleep = true;
    results.push_back(run_enhanced_test("AdaptiveLoadBalancing + adaptive sleep", loadbalance_config_sleep, test_duration));
    
    std::cout << "\n⚖️ IMBALANCED FANOUT TESTS (light/heavy/very-light paths):" << std::endl;
    std::cout << "Pipeline: Source -> Fanout -> [Gain->Sink, Noise+Gain->Sink, DirectSink] (8 blocks)" << std::endl;
    std::cout << "Expected: AdaptiveLoadBalancing should perform best due to imbalanced load" << std::endl;
    std::cout << "Adaptive sleep should help most here due to starved light paths" << std::endl;
    
    // Test 4: FixedThreadPool (should struggle with imbalanced load)
    results.push_back(run_imbalanced_test("FixedThreadPool (4 workers)", fixed_config, test_duration));
    
    // Test 4b: FixedThreadPool + Adaptive Sleep (should improve CPU efficiency)
    results.push_back(run_imbalanced_test("FixedThreadPool + adaptive sleep", fixed_config_sleep, test_duration));
    
    // Test 5: AdaptiveLoadBalancing (should excel at imbalanced)
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    // Test 5b: AdaptiveLoadBalancing + Adaptive Sleep
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing + adaptive sleep", loadbalance_config_sleep, test_duration));
    
    // Test 6: Aggressive AdaptiveLoadBalancing (should be even better)
    auto aggressive_config = cler::flowgraph_config::adaptive_load_balancing();
    aggressive_config.load_balancing_interval = 100;   // Very frequent rebalancing
    aggressive_config.load_balancing_threshold = 0.05; // Very sensitive (5% imbalance)
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing (aggressive)", aggressive_config, test_duration));
    
    // Test 6b: Aggressive AdaptiveLoadBalancing + Adaptive Sleep
    auto aggressive_config_sleep = cler::flowgraph_config::adaptive_load_balancing();
    aggressive_config_sleep.load_balancing_interval = 100;
    aggressive_config_sleep.load_balancing_threshold = 0.05;
    aggressive_config_sleep.adaptive_sleep = true;
    results.push_back(run_imbalanced_test("AdaptiveLoadBalancing (aggressive) + adaptive sleep", aggressive_config_sleep, test_duration));
    
    std::cout << "\n🚀 HEAVY FANOUT TESTS (8 parallel paths):" << std::endl;
    std::cout << "Pipeline: Source -> Fanout -> [8x Gain->Sink paths] (18 blocks total)" << std::endl;
    std::cout << "Expected: Load balancing should excel with many blocks to distribute" << std::endl;
    std::cout << "Adaptive sleep may help with thread contention and back-pressure" << std::endl;
    
    // Test 7: FixedThreadPool (should handle many blocks reasonably)
    results.push_back(run_heavy_fanout_test("FixedThreadPool (4 workers)", fixed_config, test_duration));
    
    // Test 7b: FixedThreadPool + Adaptive Sleep
    results.push_back(run_heavy_fanout_test("FixedThreadPool + adaptive sleep", fixed_config_sleep, test_duration));
    
    // Test 8: AdaptiveLoadBalancing (should excel with many blocks to balance)
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing", loadbalance_config, test_duration));
    
    // Test 8b: AdaptiveLoadBalancing + Adaptive Sleep
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing + adaptive sleep", loadbalance_config_sleep, test_duration));
    
    // Test 9: Aggressive AdaptiveLoadBalancing (should be excellent)
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing (aggressive)", aggressive_config, test_duration));
    
    // Test 9b: Aggressive AdaptiveLoadBalancing + Adaptive Sleep (ultimate config)
    results.push_back(run_heavy_fanout_test("AdaptiveLoadBalancing (aggressive) + adaptive sleep", aggressive_config_sleep, test_duration));

    // Print results
    std::cout << "========================================" << std::endl;
    std::cout << "Fanout Workload Performance Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // Analysis - All comparisons vs ThreadPerBlock baseline + with/without adaptive sleep
    std::cout << "========================================" << std::endl;
    std::cout << "Performance Analysis vs BASELINE (ThreadPerBlock)" << std::endl;
    std::cout << "With/Without Adaptive Sleep Comparisons" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (results.size() >= 5) {
        // Use baseline for all comparisons
        double baseline_throughput = baseline_result.throughput;
        double baseline_efficiency = baseline_result.cpu_efficiency;
        
        std::cout << "\n🔄 UNIFORM FANOUT Analysis:" << std::endl;
        printf("%-45s | %12s | %10s | %12s | %13s\n",
            "Configuration", "Throughput", "CPU Eff", "vs Baseline", "vs No Sleep");
        printf("%s\n", std::string(105, '-').c_str());
        
        printf("%-45s | %10.1f MS | %8.1f%% | %11s | %12s\n",
            "BASELINE (ThreadPerBlock)",
            baseline_throughput/1e6, baseline_efficiency*100, "---", "---");
        
        // FixedThreadPool comparison
        if (results.size() >= 3) {
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                "FixedThreadPool (4 workers)",
                results[1].throughput/1e6, results[1].cpu_efficiency*100,
                ((results[1].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
            
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                "FixedThreadPool + adaptive sleep",
                results[2].throughput/1e6, results[2].cpu_efficiency*100,
                ((results[2].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                ((results[2].throughput - results[1].throughput) / results[1].throughput) * 100.0);
        }
        
        // AdaptiveLoadBalancing comparison
        if (results.size() >= 5) {
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                "AdaptiveLoadBalancing",
                results[3].throughput/1e6, results[3].cpu_efficiency*100,
                ((results[3].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
            
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                "AdaptiveLoadBalancing + adaptive sleep",
                results[4].throughput/1e6, results[4].cpu_efficiency*100,
                ((results[4].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                ((results[4].throughput - results[3].throughput) / results[3].throughput) * 100.0);
        }
        
        if (results.size() >= 11) {
            std::cout << "\n\n⚖️ IMBALANCED FANOUT Analysis:" << std::endl;
            printf("%-45s | %12s | %10s | %12s | %13s\n",
                "Configuration", "Throughput", "CPU Eff", "vs Baseline", "vs No Sleep");
            printf("%s\n", std::string(105, '-').c_str());
            
            // FixedThreadPool imbalanced comparison (indices 5,6)
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                "FixedThreadPool (4 workers)",
                results[5].throughput/1e6, results[5].cpu_efficiency*100,
                ((results[5].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
            
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                "FixedThreadPool + adaptive sleep",
                results[6].throughput/1e6, results[6].cpu_efficiency*100,
                ((results[6].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                ((results[6].throughput - results[5].throughput) / results[5].throughput) * 100.0);
            
            // AdaptiveLoadBalancing imbalanced comparison (indices 7,8)
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                "AdaptiveLoadBalancing",
                results[7].throughput/1e6, results[7].cpu_efficiency*100,
                ((results[7].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
            
            printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                "AdaptiveLoadBalancing + adaptive sleep",
                results[8].throughput/1e6, results[8].cpu_efficiency*100,
                ((results[8].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                ((results[8].throughput - results[7].throughput) / results[7].throughput) * 100.0);
        }
        
        if (results.size() >= 15) {
            std::cout << "\n\n🚀 HEAVY FANOUT Analysis:" << std::endl;
            printf("%-45s | %12s | %10s | %12s | %13s\n",
                "Configuration", "Throughput", "CPU Eff", "vs Baseline", "vs No Sleep");
            printf("%s\n", std::string(105, '-').c_str());
            
            // Find the heavy fanout test indices (should start around index 11)
            size_t heavy_start = 11;  // Adjust based on actual test structure
            if (results.size() >= heavy_start + 4) {
                // FixedThreadPool heavy comparison
                printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                    "FixedThreadPool (4 workers)",
                    results[heavy_start].throughput/1e6, results[heavy_start].cpu_efficiency*100,
                    ((results[heavy_start].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
                
                printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                    "FixedThreadPool + adaptive sleep",
                    results[heavy_start+1].throughput/1e6, results[heavy_start+1].cpu_efficiency*100,
                    ((results[heavy_start+1].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                    ((results[heavy_start+1].throughput - results[heavy_start].throughput) / results[heavy_start].throughput) * 100.0);
                
                // AdaptiveLoadBalancing heavy comparison
                printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %12s\n",
                    "AdaptiveLoadBalancing",
                    results[heavy_start+2].throughput/1e6, results[heavy_start+2].cpu_efficiency*100,
                    ((results[heavy_start+2].throughput - baseline_throughput) / baseline_throughput) * 100.0, "---");
                
                printf("%-45s | %10.1f MS | %8.1f%% | %+10.1f%% | %+10.1f%%\n",
                    "AdaptiveLoadBalancing + adaptive sleep",
                    results[heavy_start+3].throughput/1e6, results[heavy_start+3].cpu_efficiency*100,
                    ((results[heavy_start+3].throughput - baseline_throughput) / baseline_throughput) * 100.0,
                    ((results[heavy_start+3].throughput - results[heavy_start+2].throughput) / results[heavy_start+2].throughput) * 100.0);
            }
        }
        
        // Find best per category
        std::cout << "\n🏆 WINNERS BY CATEGORY:" << std::endl;
        printf("%-20s | %-20s | %-45s | %12s | %10s\n",
            "Category", "Metric", "Configuration", "Throughput", "CPU Eff");
        printf("%s\n", std::string(115, '-').c_str());
        
        // UNIFORM fanout winners (indices 0-4, including baseline)
        if (results.size() >= 5) {
            auto uniform_throughput_best = std::max_element(results.begin(), results.begin() + 5,
                [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
            auto uniform_efficiency_best = std::max_element(results.begin(), results.begin() + 5,
                [](const TestResult& a, const TestResult& b) { return a.cpu_efficiency < b.cpu_efficiency; });
            
            // Clean configuration names (remove category-specific suffixes)
            std::string uniform_throughput_name = uniform_throughput_best->name;
            std::string uniform_efficiency_name = uniform_efficiency_best->name;
            
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Uniform", "Throughput", uniform_throughput_name.c_str(), 
                uniform_throughput_best->throughput/1e6, uniform_throughput_best->cpu_efficiency*100);
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Uniform", "CPU Efficiency", uniform_efficiency_name.c_str(), 
                uniform_efficiency_best->throughput/1e6, uniform_efficiency_best->cpu_efficiency*100);
        }
        
        // IMBALANCED fanout winners (includes baseline at index 0, imbalanced at indices 5-10)
        if (results.size() >= 11) {
            std::cout << std::endl;  // Add spacing between categories
            // Compare baseline (index 0) with imbalanced results (indices 5-10)
            auto imbalanced_candidates_throughput = std::max_element(results.begin() + 5, results.begin() + 11,
                [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
            auto imbalanced_throughput_best = (baseline_result.throughput > imbalanced_candidates_throughput->throughput) 
                ? &baseline_result : &(*imbalanced_candidates_throughput);
            
            auto imbalanced_candidates_efficiency = std::max_element(results.begin() + 5, results.begin() + 11,
                [](const TestResult& a, const TestResult& b) { return a.cpu_efficiency < b.cpu_efficiency; });
            auto imbalanced_efficiency_best = (baseline_result.cpu_efficiency > imbalanced_candidates_efficiency->cpu_efficiency)
                ? &baseline_result : &(*imbalanced_candidates_efficiency);
            
            // Clean configuration names (remove [IMBALANCED] suffix)
            std::string imbalanced_throughput_name = imbalanced_throughput_best->name;
            std::string imbalanced_efficiency_name = imbalanced_efficiency_best->name;
            size_t pos = imbalanced_throughput_name.find(" [IMBALANCED]");
            if (pos != std::string::npos) imbalanced_throughput_name.erase(pos);
            pos = imbalanced_efficiency_name.find(" [IMBALANCED]");
            if (pos != std::string::npos) imbalanced_efficiency_name.erase(pos);
            
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Imbalanced", "Throughput", imbalanced_throughput_name.c_str(), 
                imbalanced_throughput_best->throughput/1e6, imbalanced_throughput_best->cpu_efficiency*100);
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Imbalanced", "CPU Efficiency", imbalanced_efficiency_name.c_str(), 
                imbalanced_efficiency_best->throughput/1e6, imbalanced_efficiency_best->cpu_efficiency*100);
        }
        
        // HEAVY fanout winners (includes baseline at index 0, heavy at indices 11+)
        if (results.size() >= 17) {
            std::cout << std::endl;  // Add spacing between categories
            // Compare baseline (index 0) with heavy results (indices 11+)
            auto heavy_candidates_throughput = std::max_element(results.begin() + 11, results.end(),
                [](const TestResult& a, const TestResult& b) { return a.throughput < b.throughput; });
            auto heavy_throughput_best = (baseline_result.throughput > heavy_candidates_throughput->throughput)
                ? &baseline_result : &(*heavy_candidates_throughput);
            
            auto heavy_candidates_efficiency = std::max_element(results.begin() + 11, results.end(),
                [](const TestResult& a, const TestResult& b) { return a.cpu_efficiency < b.cpu_efficiency; });
            auto heavy_efficiency_best = (baseline_result.cpu_efficiency > heavy_candidates_efficiency->cpu_efficiency)
                ? &baseline_result : &(*heavy_candidates_efficiency);
            
            // Clean configuration names (remove [HEAVY FANOUT] suffix)
            std::string heavy_throughput_name = heavy_throughput_best->name;
            std::string heavy_efficiency_name = heavy_efficiency_best->name;
            size_t pos = heavy_throughput_name.find(" [HEAVY FANOUT]");
            if (pos != std::string::npos) heavy_throughput_name.erase(pos);
            pos = heavy_efficiency_name.find(" [HEAVY FANOUT]");
            if (pos != std::string::npos) heavy_efficiency_name.erase(pos);
            
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Heavy", "Throughput", heavy_throughput_name.c_str(), 
                heavy_throughput_best->throughput/1e6, heavy_throughput_best->cpu_efficiency*100);
            printf("%-20s | %-20s | %-45s | %10.1f MS | %8.1f%%\n",
                "Heavy", "CPU Efficiency", heavy_efficiency_name.c_str(), 
                heavy_efficiency_best->throughput/1e6, heavy_efficiency_best->cpu_efficiency*100);
        }
        
        std::cout << "\nNOTE: CPU Efficiency = successful_procedures / (successful_procedures + failed_procedures)" << std::endl;
    }
    
    std::cout << "========================================" << std::endl;
    
    return 0;
}