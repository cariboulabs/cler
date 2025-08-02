#include "cler.hpp"
#include "cler_utils.hpp"
#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstring>

constexpr size_t BUFFER_SIZE = 32768;  // 32KB for fair DBF comparison
constexpr size_t TEST_SAMPLES = 10'000'000;  // 10M samples for stable measurements

struct TestResult {
    std::string technique;
    double throughput;
    double duration;
    size_t samples;
    
    void print() const {
        std::cout << "=== " << technique << " ===" << std::endl;
        std::cout << "  Samples: " << samples << std::endl;
        std::cout << "  Duration: " << duration << " seconds" << std::endl;
        std::cout << "  Throughput: " << throughput << " samples/sec" << std::endl;
        std::cout << "  Performance: " << (throughput / 1e6) << " MSamples/sec" << std::endl;
        std::cout << std::endl;
    }
};

// Common source block - generates consistent data
struct SourceBlock : public cler::BlockBase {
    SourceBlock(const std::string& name)
        : BlockBase(name), _buffer_ptr(new float[BUFFER_SIZE]) {
        std::fill(_buffer_ptr, _buffer_ptr + BUFFER_SIZE, 1.0f);
    }
    
    ~SourceBlock() {
        delete[] _buffer_ptr;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Try DBF write first for better performance
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_size > 0) {
            size_t to_write = std::min(write_size, BUFFER_SIZE);
            std::memcpy(write_ptr, _buffer_ptr, to_write * sizeof(float));
            out->commit_write(to_write);
        } else {
            // Fallback to writeN if DBF not available
            size_t to_write = std::min({out->space(), BUFFER_SIZE});
            if (to_write == 0) return cler::Error::NotEnoughSpace;
            out->writeN(_buffer_ptr, to_write);
        }
        return cler::Empty{};
    }

private:
    float* _buffer_ptr;  // Heap allocation for large buffer
};

// Technique 1: Push/Pop (single sample) - SLOWEST
struct PushPopBlock : public cler::BlockBase {
    cler::Channel<float> in;

    PushPopBlock(const std::string& name)
        : BlockBase(name), in(std::max(BUFFER_SIZE, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t available = std::min({in.size(), out->space()});
        if (available == 0) return cler::Error::NotEnoughSamples;
        
        // Process samples one by one (inefficient)
        for (size_t i = 0; i < available; ++i) {
            float sample;
            in.pop(sample);
            // Simple processing
            sample *= 1.1f;
            out->push(sample);
        }
        
        return cler::Empty{};
    }
};

// Technique 2: ReadN/WriteN (bulk transfer) - GOOD
struct BulkTransferBlock : public cler::BlockBase {
    cler::Channel<float> in;

    BulkTransferBlock(const std::string& name)
        : BlockBase(name), in(std::max(BUFFER_SIZE, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))), _buffer_ptr(new float[BUFFER_SIZE]) {}
    
    ~BulkTransferBlock() {
        delete[] _buffer_ptr;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min({in.size(), out->space(), BUFFER_SIZE});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        // Bulk read, process, bulk write
        in.readN(_buffer_ptr, transferable);
        
        // Process buffer
        for (size_t i = 0; i < transferable; ++i) {
            _buffer_ptr[i] *= 1.1f;
        }
        
        out->writeN(_buffer_ptr, transferable);
        return cler::Empty{};
    }

private:
    float* _buffer_ptr;  // Heap allocation for large buffer
};

// Technique 3: Peek/Commit (TRUE zero-copy) - BETTER
struct PeekCommitBlock : public cler::BlockBase {
    cler::Channel<float> in;

    PeekCommitBlock(const std::string& name)
        : BlockBase(name), in(std::max(BUFFER_SIZE, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Peek at input data
        const float* in_ptr1, *in_ptr2;
        size_t in_size1, in_size2;
        size_t available = in.peek_read(in_ptr1, in_size1, in_ptr2, in_size2);
        
        if (available == 0) return cler::Error::NotEnoughSamples;
        
        // Peek at output buffer for writing
        float* out_ptr1, *out_ptr2;
        size_t out_size1, out_size2;
        size_t writable = out->peek_write(out_ptr1, out_size1, out_ptr2, out_size2);
        
        if (writable == 0) return cler::Error::NotEnoughSpace;
        
        // TRUE ZERO-COPY: Process directly from input to output buffers
        size_t to_process = std::min(available, writable);
        size_t processed = 0;
        
        // Helper lambda for processing between any two pointers
        auto process_chunk = [](const float* src, float* dst, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                dst[i] = src[i] * 1.1f;
            }
        };
        
        // Case 1: Both have contiguous first segments
        size_t chunk1 = std::min({in_size1, out_size1, to_process});
        if (chunk1 > 0) {
            process_chunk(in_ptr1, out_ptr1, chunk1);
            processed += chunk1;
        }
        
        // Case 2: Input wraps, output still in first segment
        if (processed < to_process && in_size2 > 0 && processed < out_size1) {
            size_t chunk2 = std::min({in_size2, out_size1 - processed, to_process - processed});
            process_chunk(in_ptr2, out_ptr1 + processed, chunk2);
            processed += chunk2;
        }
        
        // Case 3: Output wraps, input still in first segment
        if (processed < to_process && out_size2 > 0 && processed < in_size1) {
            size_t chunk3 = std::min({in_size1 - processed, out_size2, to_process - processed});
            process_chunk(in_ptr1 + processed, out_ptr2, chunk3);
            processed += chunk3;
        }
        
        // Case 4: Both wrap to second segments
        if (processed < to_process && in_size2 > 0 && out_size2 > 0) {
            size_t in_offset = (processed < in_size1) ? 0 : processed - in_size1;
            size_t out_offset = (processed < out_size1) ? 0 : processed - out_size1;
            size_t chunk4 = std::min({in_size2 - in_offset, out_size2 - out_offset, to_process - processed});
            process_chunk(in_ptr2 + in_offset, out_ptr2 + out_offset, chunk4);
            processed += chunk4;
        }
        
        // Commit both read and write
        in.commit_read(processed);
        out->commit_write(processed);
        
        return cler::Empty{};
    }
};

// Technique 4: Doubly-mapped buffer (read_dbf/write_dbf) - BEST
struct DoublyMappedBlock : public cler::BlockBase {
    cler::Channel<float> in;

    DoublyMappedBlock(const std::string& name)
        : BlockBase(name), in(std::max(BUFFER_SIZE, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)))
        {} 

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Zero-copy doubly-mapped read
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size == 0) return cler::Error::NotEnoughSamples;
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_size == 0) return cler::Error::NotEnoughSpace;
        
        size_t to_process = std::min({read_size, write_size, BUFFER_SIZE});
        
        // Zero-copy write
        for (size_t i = 0; i < to_process; ++i) {
            write_ptr[i] = read_ptr[i] * 1.1f;  // Simple processing
        }

        // Commit both read and write
        in.commit_read(to_process);
        out->commit_write(to_process);
        
        return cler::Empty{};
    }
};

// Common sink block - measures throughput
struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const std::string& name)
        : BlockBase(name), in(std::max(BUFFER_SIZE, cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float))) {
        _start_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_read = std::min(in.size(), BUFFER_SIZE);
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
    
    double get_duration() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - _start_time).count();
    }
    
    size_t get_samples_processed() const {
        return _received;
    }
    
    void reset_counters() {
        _received = 0;
        _start_time = std::chrono::steady_clock::now();
    }

private:
    size_t _received = 0;
    std::chrono::steady_clock::time_point _start_time;
};

template<typename ProcessingBlock>
TestResult run_technique_test(const std::string& technique_name) {
    std::cout << "Testing " << technique_name << "..." << std::flush;
    
    SourceBlock source("Source");
    ProcessingBlock processor("Processor");
    SinkBlock sink("Sink");

    // Warm-up period to prime caches and complete any lazy initialization
    const auto warmup_duration = std::chrono::milliseconds(500);
    const auto warmup_end = std::chrono::steady_clock::now() + warmup_duration;
    
    while (std::chrono::steady_clock::now() < warmup_end) {
        // Source -> Processor
        source.procedure(&processor.in);
        
        // Processor -> Sink
        processor.procedure(&sink.in);
        
        // Sink consumes
        sink.procedure();
    }
    
    // Reset sink counters after warmup
    sink.reset_counters();

    // Actual measurement period
    const auto test_duration = std::chrono::seconds(3);
    const auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < test_duration) {
        // Source -> Processor
        source.procedure(&processor.in);
        
        // Processor -> Sink
        processor.procedure(&sink.in);
        
        // Sink consumes
        sink.procedure();
    }
    
    std::cout << " DONE" << std::endl;
    
    return {
        technique_name,
        sink.get_throughput(),
        sink.get_duration(),
        sink.get_samples_processed()
    };
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Cler Read/Write Techniques Performance Test" << std::endl;
    std::cout << "Mode: STREAMLINED (no threading overhead)" << std::endl;
    std::cout << "Pipeline: Source -> Processor -> Sink (3 blocks)" << std::endl;
    std::cout << "Warmup: 500ms before measurement" << std::endl;
    std::cout << "Test Duration: 3 seconds per technique" << std::endl;
    std::cout << "Processing: Simple gain (x1.1) per sample" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Test different read/write techniques
    results.push_back(run_technique_test<PushPopBlock>("Push/Pop (single sample)"));
    results.push_back(run_technique_test<BulkTransferBlock>("ReadN/WriteN (bulk transfer)"));
    results.push_back(run_technique_test<PeekCommitBlock>("Peek/Commit (zero-copy read)"));
    results.push_back(run_technique_test<DoublyMappedBlock>("read/write dbf (doubly-mapped buffer)"));
    
    // Print individual results
    std::cout << "========================================" << std::endl;
    std::cout << "Individual Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // Performance comparison
    std::cout << "========================================" << std::endl;
    std::cout << "Performance Comparison" << std::endl;
    std::cout << "========================================" << std::endl;
    
    printf("%-45s | %10s | %13s\n",
        "Technique", "Throughput", "vs Push/Pop");
    printf("%s\n", std::string(74, '-').c_str());
    
    double baseline_throughput = results[0].throughput;  // Push/Pop baseline
    
    for (const auto& result : results) {
        double improvement = ((result.throughput - baseline_throughput) / baseline_throughput) * 100.0;
        printf("%-45s | %10.1f MS | %+13.1f%%\n",
            result.technique.c_str(),
            result.throughput/1e6,
            improvement);
    }
    
    // Find best technique
    auto best = std::max_element(results.begin(), results.end(), 
        [](const TestResult& a, const TestResult& b) {
            return a.throughput < b.throughput;
        });
    
    std::cout << "\n🏆 BEST TECHNIQUE:" << std::endl;
    printf("%-15s | %-45s | %12s\n",
        "Metric", "Technique", "Throughput");
    printf("%s\n", std::string(75, '-').c_str());
    
    printf("%-15s | %-45s | %10.1f MS\n",
        "Best Throughput",
        best->technique.c_str(),
        best->throughput/1e6);
    
    // Analysis summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Analysis Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "Technique Ranking (by throughput):" << std::endl;
    
    // Sort results by throughput
    auto sorted_results = results;
    std::sort(sorted_results.begin(), sorted_results.end(),
        [](const TestResult& a, const TestResult& b) {
            return a.throughput > b.throughput;
        });
    
    for (size_t i = 0; i < sorted_results.size(); ++i) {
        double improvement_vs_worst = ((sorted_results[i].throughput - results[0].throughput) / results[0].throughput) * 100.0;
        std::cout << (i + 1) << ". " << sorted_results[i].technique 
                  << " (" << std::fixed << std::setprecision(1) << (sorted_results[i].throughput / 1e6) << " MSamples/sec, "
                  << std::showpos << std::setprecision(1) << improvement_vs_worst << std::noshowpos << "% vs Push/Pop)" << std::endl;
    }
    
    std::cout << "\nRecommendations:" << std::endl;
    std::cout << "• Avoid push/pop for performance-critical paths" << std::endl;
    std::cout << "• Use readN/writeN for good baseline performance" << std::endl;
    std::cout << "• Use peek/commit for zero-copy read optimization" << std::endl;
    std::cout << "• Use read_dbf/write_dbf for maximum performance (when available)" << std::endl;
    
    // Platform-specific notes
    std::cout << "\nPlatform Notes:" << std::endl;
    bool platform_supports = cler::platform::supports_doubly_mapped_buffers();
    std::cout << "• Doubly-mapped buffers supported: " << (platform_supports ? "Yes" : "No") << std::endl;
    if (platform_supports) {
        std::cout << "• Page size: " << cler::platform::get_page_size() << " bytes" << std::endl;
        std::cout << "• Use buffers ≥32KB for automatic doubly-mapped allocation" << std::endl;
    } else {
        std::cout << "• Doubly-mapped techniques fall back to peek/commit" << std::endl;
    }
    
    std::cout << "========================================" << std::endl;
    
    return 0;
}