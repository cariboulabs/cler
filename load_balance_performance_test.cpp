#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>

constexpr size_t BUFFER_SIZE = 1024;

// Source block that produces constant data
struct SourceBlock : public cler::BlockBase {
    SourceBlock(std::string name)
        : BlockBase(std::move(name)) {
        std::fill(_buffer, _buffer + BUFFER_SIZE, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t to_write = std::min(out->space(), BUFFER_SIZE);
        if (to_write > 0) {
            out->writeN(_buffer, to_write);
        }
        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

// Processing block with variable workload to create imbalance
struct VariableWorkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    VariableWorkBlock(std::string name, size_t work_units)
        : BlockBase(std::move(name)), in(BUFFER_SIZE), 
          _work_units(work_units), _rng(std::random_device{}()), _dist(1, 512) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t chunk = _dist(_rng);
        size_t transferable = std::min({in.size(), out->space(), chunk});

        if (transferable == 0) return cler::Error::NotEnoughSamples;

        // Variable workload - some blocks do more processing
        for (size_t work = 0; work < _work_units; ++work) {
            volatile float dummy = 0.0f;  // Prevent optimization
            for (size_t i = 0; i < transferable; ++i) {
                dummy += i * 0.001f;  // Simulated work
            }
        }

        in.readN(_tmp, transferable);
        
        // Simple processing
        for (size_t i = 0; i < transferable; ++i) {
            _tmp[i] *= 1.1f;  // Simple gain
        }
        
        out->writeN(_tmp, transferable);
        return cler::Empty{};
    }

private:
    float _tmp[BUFFER_SIZE];
    size_t _work_units;
    std::mt19937 _rng;
    std::uniform_int_distribution<size_t> _dist;
};

// Sink block to consume data
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

void run_performance_test(const std::string& test_name, cler::EnhancedFlowGraphConfig config) {
    constexpr size_t SAMPLES = 128'000'000;

    std::cout << "\n" << test_name << ":\n";
    std::cout << std::string(50, '=') << "\n";

    SourceBlock source("Source");
    
    // Create blocks with varying workloads to test load balancing
    VariableWorkBlock stage0("Stage0", 1);   // Light work
    VariableWorkBlock stage1("Stage1", 5);   // Heavy work  
    VariableWorkBlock stage2("Stage2", 1);   // Light work
    VariableWorkBlock stage3("Stage3", 3);   // Medium work
    VariableWorkBlock stage4("Stage4", 8);   // Very heavy work
    VariableWorkBlock stage5("Stage5", 1);   // Light work
    
    SinkBlock sink("Sink", SAMPLES);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &stage0.in),
        cler::BlockRunner(&stage0, &stage1.in),
        cler::BlockRunner(&stage1, &stage2.in),
        cler::BlockRunner(&stage2, &stage3.in),
        cler::BlockRunner(&stage3, &stage4.in),
        cler::BlockRunner(&stage4, &stage5.in),
        cler::BlockRunner(&stage5, &sink.in),
        cler::BlockRunner(&sink)
    );

    std::cout << "Configuration: ";
    switch (config.scheduler) {
        case cler::SchedulerType::ThreadPerBlock:
            std::cout << "ThreadPerBlock (baseline)";
            break;
        case cler::SchedulerType::FixedThreadPool:
            std::cout << "FixedThreadPool (" << config.num_workers << " workers)";
            break;
        case cler::SchedulerType::AdaptiveLoadBalancing:
            std::cout << "AdaptiveLoadBalancing (" << config.num_workers << " workers, ";
            std::cout << "rebalance_interval=" << config.rebalance_interval << ", ";
            std::cout << "threshold=" << config.load_balance_threshold << ")";
            break;
        default:
            std::cout << "Unknown";
    }
    std::cout << "\n";

    fg.run(config);

    // Wait for completion
    while (!sink.is_done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fg.stop();

    double throughput = sink.get_throughput();
    std::cout << "Throughput: " << std::fixed << std::setprecision(1) 
              << throughput / 1e6 << " MSamples/sec\n";

    // Print execution statistics
    const auto& stats = fg.stats();
    std::cout << "\nBlock Statistics:\n";
    for (const auto& stat : stats) {
        if (stat.successful_procedures > 0) {
            std::cout << "  " << std::setw(10) << stat.name.c_str() 
                      << ": " << std::setw(12) << stat.successful_procedures << " calls, "
                      << std::setw(8) << std::fixed << std::setprecision(2) 
                      << stat.total_runtime_s << "s runtime\n";
        }
    }
}

int main() {
    std::cout << "Cler Load Balancing Performance Test\n";
    std::cout << "====================================\n";

    // Test 1: Baseline - Thread per block
    cler::EnhancedFlowGraphConfig baseline_config;
    baseline_config.scheduler = cler::SchedulerType::ThreadPerBlock;
    run_performance_test("Baseline: ThreadPerBlock", baseline_config);

    // Test 2: Fixed thread pool  
    cler::EnhancedFlowGraphConfig threadpool_config;
    threadpool_config.scheduler = cler::SchedulerType::FixedThreadPool;
    threadpool_config.num_workers = 4;
    run_performance_test("FixedThreadPool (4 workers)", threadpool_config);

    // Test 3: Adaptive load balancing
    cler::EnhancedFlowGraphConfig loadbalance_config = cler::EnhancedFlowGraphConfig::adaptive_load_balancing();
    loadbalance_config.num_workers = 4;
    loadbalance_config.rebalance_interval = 500;   // Rebalance every 500 iterations
    loadbalance_config.load_balance_threshold = 0.3; // 30% imbalance triggers rebalancing
    run_performance_test("AdaptiveLoadBalancing (4 workers)", loadbalance_config);

    // Test 4: More aggressive load balancing
    cler::EnhancedFlowGraphConfig aggressive_config = cler::EnhancedFlowGraphConfig::adaptive_load_balancing();
    aggressive_config.num_workers = 4;
    aggressive_config.rebalance_interval = 200;   // More frequent rebalancing
    aggressive_config.load_balance_threshold = 0.15; // Lower threshold
    run_performance_test("AdaptiveLoadBalancing (aggressive)", aggressive_config);

    return 0;
}