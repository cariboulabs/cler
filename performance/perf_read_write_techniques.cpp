#include "cler.hpp"
#include "cler_utils.hpp"
#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <numeric>
#include <map>
#include <type_traits>
#include <fstream>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

constexpr size_t BUFFER_SIZE = 32768;  // 32KB default
constexpr size_t SMALL_BUFFER = 8192;  // 8KB for wraparound scenarios
constexpr size_t LARGE_BUFFER = 262144; // 256KB for bulk scenarios

struct TestResult {
    std::string technique;
    std::string scenario;
    std::vector<double> throughputs;  // All runs
    
    double get_mean() const {
        return std::accumulate(throughputs.begin(), throughputs.end(), 0.0) / throughputs.size();
    }
    
    double get_std_dev() const {
        double mean = get_mean();
        double variance = 0.0;
        for (double t : throughputs) {
            double diff = t - mean;
            variance += diff * diff;
        }
        return std::sqrt(variance / throughputs.size());
    }
    
    double get_best() const {
        return *std::max_element(throughputs.begin(), throughputs.end());
    }
};

// Common source blocks
struct SourceBlock : public cler::BlockBase {
    SourceBlock(const std::string& name, size_t chunk_size = BUFFER_SIZE)
        : BlockBase(name), _chunk_size(chunk_size), _buffer_ptr(new float[chunk_size]) {
        std::fill(_buffer_ptr, _buffer_ptr + chunk_size, 1.0f);
    }
    
    ~SourceBlock() {
        delete[] _buffer_ptr;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr && write_size > 0) {
            size_t to_write = std::min(write_size, _chunk_size);
            std::memcpy(write_ptr, _buffer_ptr, to_write * sizeof(float));
            out->commit_write(to_write);
        }
        return cler::Empty{};
    }

private:
    size_t _chunk_size;
    float* _buffer_ptr;
};

// Variable size source
struct VariableSourceBlock : public cler::BlockBase {
    VariableSourceBlock(const std::string& name)
        : BlockBase(name), _buffer_ptr(new float[MAX_CHUNK]), 
          _rng(std::random_device{}()), _dist(1024, MAX_CHUNK) {
        std::fill(_buffer_ptr, _buffer_ptr + MAX_CHUNK, 1.0f);
    }
    
    ~VariableSourceBlock() {
        delete[] _buffer_ptr;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t chunk = _dist(_rng);
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr && write_size >= chunk) {
            std::memcpy(write_ptr, _buffer_ptr, chunk * sizeof(float));
            out->commit_write(chunk);
        }
        return cler::Empty{};
    }

private:
    static constexpr size_t MAX_CHUNK = 131072;  // 128KB max
    float* _buffer_ptr;
    std::mt19937 _rng;
    std::uniform_int_distribution<size_t> _dist;
};

// Processing complexity levels
enum class ProcessingComplexity {
    None,      // Pure memcpy
    Minimal,   // Simple scale
    Normal,    // Polynomial (our standard)
    Complex    // Expensive computation
};

// Base processing block template
template<ProcessingComplexity complexity>
struct ProcessingBlock : public cler::BlockBase {
    cler::Channel<float> in;

    ProcessingBlock(const std::string& name, size_t buffer_size)
        : BlockBase(name), 
          in(std::max(buffer_size, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))),
          _buffer_size(buffer_size),
          _buffer_ptr(new float[buffer_size]) {}
    
    ~ProcessingBlock() {
        delete[] _buffer_ptr;
    }

    void process_data(const float* src, float* dst, size_t count) {
        if constexpr (complexity == ProcessingComplexity::None) {
            std::memcpy(dst, src, count * sizeof(float));
        } else if constexpr (complexity == ProcessingComplexity::Minimal) {
            for (size_t i = 0; i < count; ++i) {
                dst[i] = src[i] * 0.95f;
            }
        } else if constexpr (complexity == ProcessingComplexity::Normal) {
            for (size_t i = 0; i < count; ++i) {
                float val = src[i];
                val = val * 1.1f + 0.1f;
                val = val * val - val;
                dst[i] = val;
            }
        } else { // Complex
            for (size_t i = 0; i < count; ++i) {
                float val = src[i];
                // Simulate expensive DSP operation
                for (int j = 0; j < 10; ++j) {
                    val = std::sin(val) * std::cos(val * 2.0f);
                    val = val * 1.01f + 0.01f;
                }
                dst[i] = val;
            }
        }
    }

protected:
    size_t _buffer_size;
    float* _buffer_ptr;
};

// Technique implementations
template<ProcessingComplexity complexity>
struct BulkTransferBlock : public ProcessingBlock<complexity> {
    using Base = ProcessingBlock<complexity>;
    using Base::in;
    using Base::_buffer_size;
    using Base::_buffer_ptr;

    BulkTransferBlock(const std::string& name, size_t buffer_size)
        : Base(name, buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        in.readN(_buffer_ptr, transferable);
        this->process_data(_buffer_ptr, _buffer_ptr, transferable);
        out->writeN(_buffer_ptr, transferable);
        
        return cler::Empty{};
    }
};

template<ProcessingComplexity complexity>
struct PeekCommitBlock : public ProcessingBlock<complexity> {
    using Base = ProcessingBlock<complexity>;
    using Base::in;

    PeekCommitBlock(const std::string& name, size_t buffer_size)
        : Base(name, buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        const float* in_ptr1, *in_ptr2;
        size_t in_size1, in_size2;
        size_t available = in.peek_read(in_ptr1, in_size1, in_ptr2, in_size2);
        
        if (available == 0) return cler::Error::NotEnoughSamples;
        
        float* out_ptr1, *out_ptr2;
        size_t out_size1, out_size2;
        size_t writable = out->peek_write(out_ptr1, out_size1, out_ptr2, out_size2);
        
        if (writable == 0) return cler::Error::NotEnoughSpace;
        
        size_t to_process = std::min(available, writable);
        size_t processed = 0;
        
        // Process first segments
        size_t chunk1 = std::min({in_size1, out_size1, to_process});
        if (chunk1 > 0) {
            this->process_data(in_ptr1, out_ptr1, chunk1);
            processed += chunk1;
        }
        
        // Handle wraparound cases
        if (processed < to_process && in_size2 > 0 && processed < out_size1) {
            size_t chunk2 = std::min({in_size2, out_size1 - processed, to_process - processed});
            this->process_data(in_ptr2, out_ptr1 + processed, chunk2);
            processed += chunk2;
        }
        
        if (processed < to_process && out_size2 > 0 && processed < in_size1) {
            size_t chunk3 = std::min({in_size1 - processed, out_size2, to_process - processed});
            this->process_data(in_ptr1 + processed, out_ptr2, chunk3);
            processed += chunk3;
        }
        
        if (processed < to_process && in_size2 > 0 && out_size2 > 0) {
            size_t in_offset = (processed < in_size1) ? 0 : processed - in_size1;
            size_t out_offset = (processed < out_size1) ? 0 : processed - out_size1;
            size_t chunk4 = std::min({in_size2 - in_offset, out_size2 - out_offset, to_process - processed});
            this->process_data(in_ptr2 + in_offset, out_ptr2 + out_offset, chunk4);
            processed += chunk4;
        }
        
        in.commit_read(processed);
        out->commit_write(processed);
        
        return cler::Empty{};
    }
};

template<ProcessingComplexity complexity>
struct DoublyMappedBlock : public ProcessingBlock<complexity> {
    using Base = ProcessingBlock<complexity>;
    using Base::in;
    bool limit_chunk_size;
    size_t chunk_limit;

    DoublyMappedBlock(const std::string& name, size_t buffer_size, bool limit_chunk = true)
        : Base(name, buffer_size),
          limit_chunk_size(limit_chunk),
          chunk_limit(buffer_size) {} 

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size == 0) return cler::Error::NotEnoughSamples;
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_size == 0) return cler::Error::NotEnoughSpace;
        
        size_t to_process = std::min(read_size, write_size);
        if (limit_chunk_size) {
            to_process = std::min(to_process, chunk_limit);
        }
        
        this->process_data(read_ptr, write_ptr, to_process);

        in.commit_read(to_process);
        out->commit_write(to_process);
        
        return cler::Empty{};
    }
};

// For pipeline test, just use 3x the same processing
template<typename TechniqueBlock>
struct ThreeStagePipeline : public cler::BlockBase {
    cler::Channel<float> in;
    TechniqueBlock processor;

    ThreeStagePipeline(const std::string& name, size_t buffer_size)
        : BlockBase(name),
          in(std::max(buffer_size, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))),
          processor("Processor", buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Move data from our input to processor's input
        size_t available = in.size();
        size_t space = processor.in.space();
        size_t to_transfer = std::min(available, space);
        if (to_transfer > 0) {
            auto [read_ptr, read_size] = in.read_dbf();
            auto [write_ptr, write_size] = processor.in.write_dbf();
            size_t actual = std::min({read_size, write_size, to_transfer});
            if (actual > 0) {
                std::memcpy(write_ptr, read_ptr, actual * sizeof(float));
                in.commit_read(actual);
                processor.in.commit_write(actual);
            }
        }
        
        // Process 3 times to simulate pipeline
        for (int i = 0; i < 3; ++i) {
            processor.procedure(out);
        }
        return cler::Empty{};
    }
};

// Sink block
struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const std::string& name, size_t buffer_size)
        : BlockBase(name), 
          in(std::max(buffer_size, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))) {
        _start_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_read = in.size();
        if (to_read == 0) return cler::Error::NotEnoughSamples;

        in.commit_read(to_read);
        _received += to_read;

        return cler::Empty{};
    }

    double get_throughput() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - _start_time).count();
        return elapsed > 0.0 ? _received / elapsed : 0.0;
    }
    
    void reset_counters() {
        _received = 0;
        _start_time = std::chrono::steady_clock::now();
    }

private:
    size_t _received = 0;
    std::chrono::steady_clock::time_point _start_time;
};

// Test runner
template<typename SourceBlockType, typename ProcessingBlockType>
TestResult run_technique_test(const std::string& technique_name, 
                            const std::string& scenario_name,
                            size_t buffer_size,
                            int num_runs = 3) {
    std::cout << "Testing " << technique_name << " (" << scenario_name << ")..." << std::flush;
    
    TestResult result;
    result.technique = technique_name;
    result.scenario = scenario_name;
    
    for (int run = 0; run < num_runs; ++run) {
        SourceBlockType source("Source");
        ProcessingBlockType processor("Processor", buffer_size);
        
        SinkBlock sink("Sink", buffer_size);

        // Warm-up period
        const auto warmup_duration = std::chrono::milliseconds(500);
        const auto warmup_end = std::chrono::steady_clock::now() + warmup_duration;
        
        while (std::chrono::steady_clock::now() < warmup_end) {
            source.procedure(&processor.in);
            processor.procedure(&sink.in);
            sink.procedure();
        }
        
        // Reset sink counters after warmup
        sink.reset_counters();

        // Actual measurement
        const auto test_duration = std::chrono::milliseconds(3000);
        const auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < test_duration) {
            source.procedure(&processor.in);
            processor.procedure(&sink.in);
            sink.procedure();
        }
        
        result.throughputs.push_back(sink.get_throughput());
    }
    
    std::cout << " DONE" << std::endl;
    return result;
}

// Special test runner for unlimited DBF
template<typename SourceBlockType, ProcessingComplexity complexity>
TestResult run_dbf_unlimited_test(const std::string& scenario_name,
                                size_t buffer_size,
                                int num_runs = 3) {
    std::cout << "Testing DBF (unlimited) (" << scenario_name << ")..." << std::flush;
    
    TestResult result;
    result.technique = "DBF (unlimited)";
    result.scenario = scenario_name;
    
    for (int run = 0; run < num_runs; ++run) {
        SourceBlockType source("Source");
        DoublyMappedBlock<complexity> processor("Processor", buffer_size);
        processor.limit_chunk_size = false;  // Unlimited processing
        
        SinkBlock sink("Sink", buffer_size);

        // Warm-up period
        const auto warmup_duration = std::chrono::milliseconds(500);
        const auto warmup_end = std::chrono::steady_clock::now() + warmup_duration;
        
        while (std::chrono::steady_clock::now() < warmup_end) {
            source.procedure(&processor.in);
            processor.procedure(&sink.in);
            sink.procedure();
        }
        
        // Reset sink counters after warmup
        sink.reset_counters();

        // Actual measurement
        const auto test_duration = std::chrono::milliseconds(1500);
        const auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < test_duration) {
            source.procedure(&processor.in);
            processor.procedure(&sink.in);
            sink.procedure();
        }
        
        result.throughputs.push_back(sink.get_throughput());
    }
    
    std::cout << " DONE" << std::endl;
    return result;
}

// Helper to read file contents
std::string read_sys_file(const std::string& path) {
    std::ifstream file(path);
    std::string content;
    if (file.is_open()) {
        std::getline(file, content);
        file.close();
    }
    return content;
}

// Get current CPU frequency in MHz
double get_cpu_freq_mhz() {
    std::string freq_str = read_sys_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (!freq_str.empty()) {
        return std::stod(freq_str) / 1000.0; // Convert kHz to MHz
    }
    return 0.0;
}

int main() {
    // Pin to CPU 0 for consistent results
    #ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
        std::cout << "Pinned to CPU 0 for consistent results" << std::endl;
    }
    #endif
    
    std::cout << "========================================" << std::endl;
    std::cout << "Cler Read/Write Techniques Performance Test" << std::endl;
    std::cout << "Mode: STREAMLINED (no threading overhead)" << std::endl;
    std::cout << "Test Duration: 1.5 seconds per technique, 3 runs each" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // System status check
    std::cout << "\n--- System Status Check ---" << std::endl;
    #ifdef __linux__
    std::string governor = read_sys_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    std::string min_freq = read_sys_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq");
    std::string max_freq = read_sys_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
    
    std::cout << "CPU Governor: " << governor << std::endl;
    if (!min_freq.empty() && !max_freq.empty()) {
        std::cout << "CPU Frequency Range: " << (std::stod(min_freq)/1000) << " - " 
                  << (std::stod(max_freq)/1000) << " MHz" << std::endl;
    }
    std::cout << "Current CPU Frequency: " << get_cpu_freq_mhz() << " MHz" << std::endl;
    
    // Check for thermal throttling
    if (governor != "performance") {
        std::cout << "⚠️  WARNING: CPU governor is not 'performance' - results may vary!" << std::endl;
        std::cout << "   For best results, run: sudo cpupower frequency-set -g performance" << std::endl;
    }
    #endif
    
    std::cout << "\n--- Scenario Explanations ---" << std::endl;
    std::cout << "1. Fixed 32KB: Process exactly 32KB chunks (typical DSP/audio)" << std::endl;
    std::cout << "2. Variable: Random-sized chunks (1KB-128KB)" << std::endl;
    std::cout << "3. Wraparound: 8KB buffer forces frequent circular buffer wrapping" << std::endl;
    std::cout << "4. Large Buffer: 256KB buffers for bulk processing" << std::endl;
    std::cout << "5. Minimal Proc: Simple scaling to highlight memory overhead" << std::endl;
    std::cout << "6. No Processing: Pure memory copy (best case for DBF)" << std::endl;
    std::cout << "7. Complex Proc: Expensive computation (10x sin/cos per sample)" << std::endl;
    
    std::vector<TestResult> results;
    
    // Scenario 1: Fixed-size chunks with normal processing
    std::cout << "\n--- Scenario 1: Fixed-Size Chunks (32KB) ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::Normal>>("ReadN/WriteN", "Fixed 32KB", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::Normal>>("Peek/Commit", "Fixed 32KB", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::Normal>>("DBF", "Fixed 32KB", BUFFER_SIZE));
    
    // Scenario 2: Variable-size processing
    std::cout << "\n--- Scenario 2: Variable-Size Processing ---" << std::endl;
    results.push_back(run_technique_test<VariableSourceBlock, BulkTransferBlock<ProcessingComplexity::Normal>>("ReadN/WriteN", "Variable", BUFFER_SIZE));
    results.push_back(run_technique_test<VariableSourceBlock, PeekCommitBlock<ProcessingComplexity::Normal>>("Peek/Commit", "Variable", BUFFER_SIZE));
    results.push_back(run_technique_test<VariableSourceBlock, DoublyMappedBlock<ProcessingComplexity::Normal>>("DBF (chunked)", "Variable", BUFFER_SIZE));
    results.push_back(run_dbf_unlimited_test<VariableSourceBlock, ProcessingComplexity::Normal>("Variable", LARGE_BUFFER));
    
    // Scenario 3: Small buffer with wraparound
    std::cout << "\n--- Scenario 3: Wraparound-Heavy (8KB buffer) ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::Normal>>("ReadN/WriteN", "Wraparound", SMALL_BUFFER));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::Normal>>("Peek/Commit", "Wraparound", SMALL_BUFFER));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::Normal>>("DBF", "Wraparound", SMALL_BUFFER));
    
    // Scenario 4: Large buffers
    std::cout << "\n--- Scenario 4: Large Buffers (256KB) ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::Normal>>("ReadN/WriteN", "Large Buffer", LARGE_BUFFER));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::Normal>>("Peek/Commit", "Large Buffer", LARGE_BUFFER));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::Normal>>("DBF", "Large Buffer", LARGE_BUFFER));
    
    // Scenario 5: Minimal processing
    std::cout << "\n--- Scenario 5: Minimal Processing ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::Minimal>>("ReadN/WriteN", "Minimal Proc", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::Minimal>>("Peek/Commit", "Minimal Proc", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::Minimal>>("DBF", "Minimal Proc", BUFFER_SIZE));
    
    // Scenario 6: No processing (pure memcpy)
    std::cout << "\n--- Scenario 6: No Processing (Pure Copy) ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::None>>("ReadN/WriteN", "No Processing", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::None>>("Peek/Commit", "No Processing", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::None>>("DBF", "No Processing", BUFFER_SIZE));
    
    // Scenario 7: Complex processing
    std::cout << "\n--- Scenario 7: Complex Processing ---" << std::endl;
    results.push_back(run_technique_test<SourceBlock, BulkTransferBlock<ProcessingComplexity::Complex>>("ReadN/WriteN", "Complex Proc", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, PeekCommitBlock<ProcessingComplexity::Complex>>("Peek/Commit", "Complex Proc", BUFFER_SIZE));
    results.push_back(run_technique_test<SourceBlock, DoublyMappedBlock<ProcessingComplexity::Complex>>("DBF", "Complex Proc", BUFFER_SIZE));
    
    // Performance comparison by scenario
    std::cout << "\n========================================" << std::endl;
    std::cout << "Performance Summary by Scenario" << std::endl;
    std::cout << "========================================" << std::endl;
    
    printf("%-20s | %-15s | %15s | %15s | %15s | %8s\n",
        "Scenario", "Technique", "Mean (MS/s)", "StdDev (MS/s)", "Best (MS/s)", "CV%");
    printf("%s\n", std::string(110, '-').c_str());
    
    for (const auto& result : results) {
        double mean = result.get_mean() / 1e6;
        double std_dev = result.get_std_dev() / 1e6;
        double best = result.get_best() / 1e6;
        double cv = (result.get_std_dev() / result.get_mean()) * 100;
        
        printf("%-20s | %-15s | %15.2f | %15.2f | %15.2f | %7.1f%%\n",
            result.scenario.c_str(),
            result.technique.c_str(),
            mean, std_dev, best, cv);
    }
    
    // Find best technique per scenario
    std::cout << "\n========================================" << std::endl;
    std::cout << "Best Technique per Scenario" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::map<std::string, std::pair<std::string, double>> best_per_scenario;
    for (const auto& result : results) {
        auto& best = best_per_scenario[result.scenario];
        double mean = result.get_mean();
        if (mean > best.second) {
            best.first = result.technique;
            best.second = mean;
        }
    }
    
    for (const auto& [scenario, best] : best_per_scenario) {
        printf("%-25s: %-20s (%10.2f MS/s)\n", 
            scenario.c_str(), 
            best.first.c_str(), 
            best.second / 1e6);
    }
    
    // Performance analysis
    std::cout << "\n===================" << std::endl;
    std::cout << "Performance Analysis" << std::endl;
    std::cout << "===================" << std::endl;

    // Calculate relative performance
    std::cout << "\nRelative Performance (vs ReadN/WriteN):" << std::endl;
    std::map<std::string, std::map<std::string, double>> scenario_results;
    for (const auto& result : results) {
        scenario_results[result.scenario][result.technique] = result.get_mean();
    }
    
    for (const auto& [scenario, techniques] : scenario_results) {
        double baseline = techniques.at("ReadN/WriteN");
        std::cout << "\n" << scenario << ":" << std::endl;
        for (const auto& [technique, throughput] : techniques) {
            double relative = ((throughput - baseline) / baseline) * 100;
            printf("  %-15s: %+6.1f%%\n", technique.c_str(), relative);
        }
    }
    
    // Final system status check
    #ifdef __linux__
    std::cout << "\n--- Final System Status ---" << std::endl;
    double final_freq = get_cpu_freq_mhz();
    std::cout << "Final CPU Frequency: " << final_freq << " MHz" << std::endl;
    
    // Check if we likely throttled
    if (final_freq < (std::stod(max_freq)/1000) * 0.9) {
        std::cout << "⚠️  WARNING: CPU may have throttled during test!" << std::endl;
        std::cout << "   Results might be affected by thermal constraints." << std::endl;
    } else {
        std::cout << "✓ CPU maintained good frequency throughout test" << std::endl;
    }
    #endif
    
    return 0;
}