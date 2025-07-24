#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <chrono>
#include <thread>

// Simple test blocks for validating enhanced configuration
struct TestSource : public cler::BlockBase {
    size_t samples_to_generate;
    size_t generated = 0;
    
    TestSource(const char* name, size_t samples) 
        : BlockBase(name), samples_to_generate(samples) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (generated >= samples_to_generate) {
            return cler::Error::TERM_EOFReached;
        }
        
        size_t to_write = std::min(out->space(), samples_to_generate - generated);
        if (to_write == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Write batch of samples
        std::vector<float> batch(to_write, 1.0f);
        out->writeN(batch.data(), to_write);
        generated += to_write;
        
        return cler::Empty{};
    }
};

struct TestProcessor : public cler::BlockBase {
    cler::Channel<float> in;
    
    TestProcessor(const char* name) : BlockBase(name), in(1024) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        size_t to_process = std::min(available, out->space());
        if (to_process == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Simple processing: read and multiply by 2
        std::vector<float> batch(to_process);
        in.readN(batch.data(), to_process);
        
        for (auto& sample : batch) {
            sample *= 2.0f;
        }
        
        out->writeN(batch.data(), to_process);
        return cler::Empty{};
    }
};

struct TestSink : public cler::BlockBase {
    cler::Channel<float> in;
    size_t consumed = 0;
    std::chrono::steady_clock::time_point start_time;
    
    TestSink(const char* name) : BlockBase(name), in(1024) {
        start_time = std::chrono::steady_clock::now();
    }
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        std::vector<float> batch(available);
        size_t read = in.readN(batch.data(), available);
        consumed += read;
        
        return cler::Empty{};
    }
    
    double get_throughput() const {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        return consumed / elapsed;
    }
};

struct TestResult {
    std::string config_name;
    double throughput_samples_per_sec;
    double duration_seconds;
    size_t total_samples;
    
    void print() const {
        std::cout << "=== " << config_name << " ===" << std::endl;
        std::cout << "  Samples: " << total_samples << std::endl;
        std::cout << "  Duration: " << duration_seconds << " seconds" << std::endl;
        std::cout << "  Throughput: " << throughput_samples_per_sec << " samples/sec" << std::endl;
        std::cout << "  Performance: " << (throughput_samples_per_sec / 1e6) << " MSamples/sec" << std::endl;
        std::cout << std::endl;
    }
};

TestResult run_test(const std::string& name, std::function<void()> test_func) {
    std::cout << "Running test: " << name << "..." << std::endl;
    
    auto start = std::chrono::steady_clock::now();
    test_func();
    auto end = std::chrono::steady_clock::now();
    
    double duration = std::chrono::duration<double>(end - start).count();
    
    return {name, 0.0, duration, 0};  // Will be filled by specific test
}

int main() {
    const size_t TEST_SAMPLES = 1000000;  // 1M samples for quick testing
    
    std::cout << "========================================" << std::endl;
    std::cout << "Enhanced Configuration Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Test 1: Legacy ThreadPerBlock mode
    {
        TestSource source("Source", TEST_SAMPLES);
        TestProcessor proc1("Proc1");
        TestProcessor proc2("Proc2");
        TestSink sink("Sink");
        
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &proc1.in),
            cler::BlockRunner(&proc1, &proc2.in),
            cler::BlockRunner(&proc2, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        auto start = std::chrono::steady_clock::now();
        
        // Use legacy configuration
        cler::FlowGraphConfig legacy_config;
        legacy_config.adaptive_sleep = true;
        flowgraph.run(legacy_config);
        
        // Wait for completion
        while (!flowgraph.is_stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(end - start).count();
        
        results.push_back({
            "Legacy ThreadPerBlock",
            sink.get_throughput(),
            duration,
            sink.consumed
        });
    }
    
    // Test 2: Enhanced FixedThreadPool (2 workers)
    {
        TestSource source("Source", TEST_SAMPLES);
        TestProcessor proc1("Proc1");
        TestProcessor proc2("Proc2");
        TestSink sink("Sink");
        
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &proc1.in),
            cler::BlockRunner(&proc1, &proc2.in),
            cler::BlockRunner(&proc2, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        auto start = std::chrono::steady_clock::now();
        
        // Use enhanced configuration
        cler::EnhancedFlowGraphConfig enhanced_config;
        enhanced_config.scheduler = cler::SchedulerType::FixedThreadPool;
        enhanced_config.num_workers = 2;
        enhanced_config.reduce_error_checks = false;  // Keep safe
        
        flowgraph.run(enhanced_config);
        
        // Wait for completion
        while (!flowgraph.is_stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(end - start).count();
        
        results.push_back({
            "Enhanced FixedThreadPool (2 workers)",
            sink.get_throughput(),
            duration,
            sink.consumed
        });
    }
    
    // Test 3: Enhanced FixedThreadPool with optimizations
    {
        TestSource source("Source", TEST_SAMPLES);
        TestProcessor proc1("Proc1");
        TestProcessor proc2("Proc2");
        TestSink sink("Sink");
        
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &proc1.in),
            cler::BlockRunner(&proc1, &proc2.in),
            cler::BlockRunner(&proc2, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        auto start = std::chrono::steady_clock::now();
        
        // Use enhanced configuration with optimizations
        cler::EnhancedFlowGraphConfig enhanced_config;
        enhanced_config.scheduler = cler::SchedulerType::FixedThreadPool;
        enhanced_config.num_workers = 3;
        enhanced_config.reduce_error_checks = true;   // Enable optimizations
        enhanced_config.min_work_threshold = 4;       // Batch small work
        
        flowgraph.run(enhanced_config);
        
        // Wait for completion
        while (!flowgraph.is_stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(end - start).count();
        
        results.push_back({
            "Enhanced FixedThreadPool (optimized)",
            sink.get_throughput(),
            duration,
            sink.consumed
        });
    }
    
    // Test 4: SingleThreaded mode
    {
        TestSource source("Source", TEST_SAMPLES / 4);  // Smaller test for single thread
        TestProcessor proc1("Proc1");
        TestSink sink("Sink");
        
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &proc1.in),
            cler::BlockRunner(&proc1, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        auto start = std::chrono::steady_clock::now();
        
        // Use single-threaded configuration
        cler::EnhancedFlowGraphConfig enhanced_config;
        enhanced_config.scheduler = cler::SchedulerType::SingleThreaded;
        enhanced_config.reduce_error_checks = true;
        
        flowgraph.run(enhanced_config);
        
        // Wait for completion
        while (!flowgraph.is_stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(end - start).count();
        
        results.push_back({
            "SingleThreaded (deterministic)",
            sink.get_throughput(),
            duration,
            sink.consumed
        });
    }
    
    // Print results
    std::cout << "========================================" << std::endl;
    std::cout << "Test Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // Calculate improvements
    if (results.size() >= 2) {
        double baseline = results[0].throughput_samples_per_sec;
        std::cout << "Performance Improvements:" << std::endl;
        for (size_t i = 1; i < results.size(); ++i) {
            double improvement = ((results[i].throughput_samples_per_sec - baseline) / baseline) * 100.0;
            std::cout << "  " << results[i].config_name << ": " 
                      << std::showpos << improvement << "%" << std::endl;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "Enhanced Configuration Tests Complete!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}