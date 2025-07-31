#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iomanip>

constexpr size_t BUFFER_SIZE = 1024;
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
        : BlockBase(name) {
        std::fill(_buffer, _buffer + BUFFER_SIZE, 1.0f);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t to_write = std::min({out->space(), BUFFER_SIZE});
        if (to_write == 0) return cler::Error::NotEnoughSpace;
        
        out->writeN(_buffer, to_write);
        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

// Technique 1: Push/Pop (single sample) - SLOWEST
struct PushPopBlock : public cler::BlockBase {
    cler::Channel<float> in;

    PushPopBlock(const std::string& name)
        : BlockBase(name), in(BUFFER_SIZE) {}

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
        : BlockBase(name), in(BUFFER_SIZE) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min({in.size(), out->space(), BUFFER_SIZE});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        // Bulk read, process, bulk write
        in.readN(_buffer, transferable);
        
        // Process buffer
        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] *= 1.1f;
        }
        
        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

// Technique 3: Peek/Commit (TRUE zero-copy) - BETTER
struct PeekCommitBlock : public cler::BlockBase {
    cler::Channel<float> in;

    PeekCommitBlock(const std::string& name)
        : BlockBase(name), in(BUFFER_SIZE) {}

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
        : BlockBase(name), in(32768) {}  // Large buffer for doubly-mapped allocation

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Try zero-copy doubly-mapped read first
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr && read_size > 0) {
            // Fast path: direct processing from doubly-mapped buffer
            size_t to_process = std::min({read_size, out->space(), BUFFER_SIZE});
            
            // Try zero-copy write
            auto [write_ptr, write_size] = out->write_dbf();
            if (write_ptr && write_size >= to_process) {
                // Ultimate fast path: direct processing between doubly-mapped buffers
                for (size_t i = 0; i < to_process; ++i) {
                    write_ptr[i] = read_ptr[i] * 1.1f;
                }
                in.commit_read(to_process);
                out->commit_write(to_process);
            } else {
                // Semi-fast path: read from doubly-mapped, write normally
                for (size_t i = 0; i < to_process; ++i) {
                    _buffer[i] = read_ptr[i] * 1.1f;
                }
                in.commit_read(to_process);
                out->writeN(_buffer, to_process);
            }
            
            return cler::Empty{};
        }
        
        // Fallback to peek/commit if doubly-mapped not available
        const float* ptr1, *ptr2;
        size_t size1, size2;
        size_t available = in.peek_read(ptr1, size1, ptr2, size2);
        
        if (available == 0) return cler::Error::NotEnoughSamples;
        
        size_t to_process = std::min({available, out->space(), BUFFER_SIZE});
        size_t processed = 0;
        
        // Process first segment
        size_t from_seg1 = std::min(size1, to_process);
        for (size_t i = 0; i < from_seg1; ++i) {
            _buffer[processed++] = ptr1[i] * 1.1f;
        }
        
        // Process second segment if needed
        if (processed < to_process && size2 > 0) {
            size_t from_seg2 = std::min(size2, to_process - processed);
            for (size_t i = 0; i < from_seg2; ++i) {
                _buffer[processed++] = ptr2[i] * 1.1f;
            }
        }
        
        out->writeN(_buffer, processed);
        in.commit_read(processed);
        
        return cler::Empty{};
    }

private:
    float _buffer[BUFFER_SIZE];
};

// Common sink block - measures throughput
struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const std::string& name)
        : BlockBase(name), in(BUFFER_SIZE) {
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

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &processor.in),
        cler::BlockRunner(&processor, &sink.in),
        cler::BlockRunner(&sink)
    );

    // Run for a fixed duration to get stable measurements
    const auto test_duration = std::chrono::seconds(3);
    fg.run_for(test_duration);
    
    // No need to calculate CPU efficiency for this test
    
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
    std::cout << "Pipeline: Source -> Processor -> Sink (3 blocks)" << std::endl;
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