#include <gtest/gtest.h>
#include "cler.hpp"
#include "cler_stdthread_policy.hpp"
#include "cler_spsc-queue.hpp"
#include "cler_embedded_allocators.hpp"
#include <atomic>
#include <chrono>
#include <complex>

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test DSP pipeline with SPSC queues and custom allocators
TEST_F(IntegrationTest, DSPPipelineWithSPSCQueues) {
    // Create custom allocator for queues
    cler::StaticPoolAllocator<16384> pool_allocator;
    
    // Create SPSC queues with custom allocator for inter-block communication
    dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>> 
        input_queue(1024, pool_allocator);
    dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>> 
        output_queue(1024, pool_allocator);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(input_queue.is_valid());
    ASSERT_TRUE(output_queue.is_valid());
#endif
    
    std::atomic<int> input_count{0};
    std::atomic<int> process_count{0};
    std::atomic<int> output_count{0};
    
    // Input block: generates sine wave samples  
    struct InputBlock : cler::BlockBase {
        std::atomic<int>& counter;
        dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>>& queue;
        float phase = 0.0f;
        
        InputBlock(std::atomic<int>& c, auto& q) 
            : cler::BlockBase("InputBlock"), counter(c), queue(q) {}
        
        cler::Result<cler::Empty, cler::Error> procedure() {
            constexpr float freq = 440.0f / 48000.0f;  // 440 Hz at 48kHz
            
            for (int i = 0; i < 32; ++i) {  // Process in chunks
                float sample = std::sin(2.0f * M_PI * phase);
                if (queue.try_push(sample)) {
                    counter++;
                    phase += freq;
                    if (phase > 1.0f) phase -= 1.0f;
                } else {
                    break;  // Queue full
                }
            }
            return cler::Empty{};
        }
    };
    
    // Processing block: applies simple gain
    struct ProcessBlock : cler::BlockBase {
        std::atomic<int>& counter;
        dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>>& input_q;
        dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>>& output_q;
        
        ProcessBlock(std::atomic<int>& c, auto& in_q, auto& out_q) 
            : cler::BlockBase("ProcessBlock"), counter(c), input_q(in_q), output_q(out_q) {}
        
        cler::Result<cler::Empty, cler::Error> procedure() {
            constexpr float gain = 0.5f;
            float sample;
            
            for (int i = 0; i < 32; ++i) {  // Process in chunks
                if (input_q.try_pop(sample)) {
                    float processed = sample * gain;
                    if (output_q.try_push(processed)) {
                        counter++;
                    } else {
                        // Put sample back if output queue is full
                        input_q.try_push(sample);
                        break;
                    }
                } else {
                    break;  // No input samples
                }
            }
            return cler::Empty{};
        }
    };
    
    // Output block: consumes processed samples
    struct OutputBlock : cler::BlockBase {
        std::atomic<int>& counter;
        dro::SPSCQueue<float, 0, cler::StaticPoolAllocator<16384>>& queue;
        
        OutputBlock(std::atomic<int>& c, auto& q) 
            : cler::BlockBase("OutputBlock"), counter(c), queue(q) {}
        
        cler::Result<cler::Empty, cler::Error> procedure() {
            float sample;
            
            for (int i = 0; i < 32; ++i) {  // Process in chunks
                if (queue.try_pop(sample)) {
                    counter++;
                    // Verify sample is within expected range (gain = 0.5)
                    if (sample < -0.5f || sample > 0.5f) {
                        return cler::Error::BadData;
                    }
                } else {
                    break;  // No output samples
                }
            }
            return cler::Empty{};
        }
    };
    
    // Create blocks
    InputBlock input_block(input_count, input_queue);
    ProcessBlock process_block(process_count, input_queue, output_queue);
    OutputBlock output_block(output_count, output_queue);
    
    // Create block runners
    cler::BlockRunner input_runner(&input_block);
    cler::BlockRunner process_runner(&process_block);
    cler::BlockRunner output_runner(&output_block);
    
    // Create flow graph
    cler::FlowGraph<cler::StdThreadPolicy, 
                    decltype(input_runner), 
                    decltype(process_runner), 
                    decltype(output_runner)> 
        flowgraph(input_runner, process_runner, output_runner);
    
    // Run the pipeline
    flowgraph.run();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    flowgraph.stop();
    
    // Verify data flowed through the pipeline
    EXPECT_GT(input_count.load(), 0);
    EXPECT_GT(process_count.load(), 0);
    EXPECT_GT(output_count.load(), 0);
    
    // Verify processing chain works
    EXPECT_LE(output_count.load(), process_count.load());
    EXPECT_LE(process_count.load(), input_count.load());
}

// Test complex number processing with stack allocators
TEST_F(IntegrationTest, ComplexProcessingWithStackAllocators) {
    using Complex = std::complex<float>;
    
    // Use stack-allocated queues for this test
    dro::SPSCQueue<Complex, 512> complex_queue;
    
    std::atomic<int> fft_input_count{0};
    std::atomic<int> fft_output_count{0};
    
    // FFT input block: generates complex sinusoids
    auto fft_input_block = [&]() -> cler::Result<void> {
        static float phase = 0.0f;
        constexpr float freq = 1.0f / 64.0f;  // 1/64 of sample rate
        
        for (int i = 0; i < 16; ++i) {
            Complex sample(std::cos(2.0f * M_PI * phase), 
                          std::sin(2.0f * M_PI * phase));
            
            if (complex_queue.try_push(sample)) {
                fft_input_count++;
                phase += freq;
                if (phase > 1.0f) phase -= 1.0f;
            } else {
                break;
            }
        }
        return cler::ok();
    };
    
    // FFT processing block: simple magnitude calculation
    auto fft_process_block = [&]() -> cler::Result<void> {
        Complex sample;
        
        for (int i = 0; i < 16; ++i) {
            if (complex_queue.try_pop(sample)) {
                float magnitude = std::abs(sample);
                fft_output_count++;
                
                // Verify magnitude is reasonable (should be ~1.0 for unit circle)
                EXPECT_GE(magnitude, 0.9f);
                EXPECT_LE(magnitude, 1.1f);
            } else {
                break;
            }
        }
        return cler::ok();
    };
    
    // Create runners and flow graph
    cler::BlockRunner input_runner(fft_input_block);
    cler::BlockRunner process_runner(fft_process_block);
    
    cler::FlowGraph<cler::StdThreadPolicy, 
                    decltype(input_runner), 
                    decltype(process_runner)> 
        flowgraph(input_runner, process_runner);
    
    // Run the pipeline
    flowgraph.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    flowgraph.stop();
    
    // Verify processing occurred
    EXPECT_GT(fft_input_count.load(), 0);
    EXPECT_GT(fft_output_count.load(), 0);
}

// Test error propagation through the system
TEST_F(IntegrationTest, ErrorPropagationThroughSystem) {
    dro::SPSCQueue<int, 256> error_queue;
    
    std::atomic<int> error_injection_count{0};
    std::atomic<int> successful_processing{0};
    
    // Block that occasionally produces errors
    auto error_prone_block = [&]() -> cler::Result<void> {
        static int counter = 0;
        counter++;
        
        if (counter % 10 == 0) {
            error_injection_count++;
            return cler::err(cler::Error::ProcedureError);
        }
        
        // Normal processing - put data in queue
        if (error_queue.try_push(counter)) {
            successful_processing++;
        }
        
        return cler::ok();
    };
    
    // Block that consumes data
    auto consumer_block = [&]() -> cler::Result<void> {
        int value;
        if (error_queue.try_pop(value)) {
            // Just consume the data
        }
        return cler::ok();
    };
    
    // Create flow graph
    cler::BlockRunner error_runner(error_prone_block);
    cler::BlockRunner consumer_runner(consumer_block);
    
    cler::FlowGraph<cler::StdThreadPolicy, 
                    decltype(error_runner), 
                    decltype(consumer_runner)> 
        flowgraph(error_runner, consumer_runner);
    
    // Run with errors
    flowgraph.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    flowgraph.stop();
    
    // Verify error handling
    EXPECT_GT(error_injection_count.load(), 0);
    EXPECT_GT(successful_processing.load(), 0);
    
    // Check statistics for error counting
    auto stats = flowgraph.get_stats();
    EXPECT_EQ(stats.size(), 2);  // Two blocks
    
    // First block should have errors
    EXPECT_GT(stats[0].error_count, 0);
    // Second block should have no errors (it doesn't produce any)
    EXPECT_EQ(stats[1].error_count, 0);
}

// Test real-time constraints with thread-safe allocators
TEST_F(IntegrationTest, RealTimeConstraintsWithThreadSafeAllocators) {
    // Use thread-safe pool allocator for deterministic allocation
    cler::ThreadSafePoolAllocator<64, 128> rt_allocator;
    
    dro::SPSCQueue<float, 0, cler::ThreadSafePoolAllocator<64, 128>> 
        rt_queue(64, rt_allocator);
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(rt_queue.is_valid());
#endif
    
    std::atomic<bool> timing_violated{false};
    std::atomic<int> rt_iterations{0};
    
    // Real-time block with strict timing requirements
    auto rt_block = [&]() -> cler::Result<void> {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Simulate real-time processing
        for (int i = 0; i < 8; ++i) {
            float sample = static_cast<float>(i) / 8.0f;
            rt_queue.try_push(sample);
            
            float retrieved;
            rt_queue.try_pop(retrieved);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Check if we exceeded our time budget (100μs)
        if (duration.count() > 100) {
            timing_violated = true;
        }
        
        rt_iterations++;
        return cler::ok();
    };
    
    // Create and run flow graph
    cler::BlockRunner rt_runner(rt_block);
    cler::FlowGraph<cler::StdThreadPolicy, decltype(rt_runner)> flowgraph(rt_runner);
    
    flowgraph.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    flowgraph.stop();
    
    // Verify real-time performance
    EXPECT_GT(rt_iterations.load(), 0);
    EXPECT_FALSE(timing_violated.load()) << "Real-time timing constraints were violated";
}

// Test system shutdown and cleanup
TEST_F(IntegrationTest, SystemShutdownAndCleanup) {
    dro::SPSCQueue<int, 128> shutdown_queue;
    
    std::atomic<bool> shutdown_requested{false};
    std::atomic<int> cleanup_count{0};
    
    // Block that monitors for shutdown
    auto monitoring_block = [&]() -> cler::Result<void> {
        if (shutdown_requested.load()) {
            cleanup_count++;
            return cler::err(cler::Error::TERMINATE_FLOWGRAPH);
        }
        
        // Normal operation
        shutdown_queue.try_push(42);
        return cler::ok();
    };
    
    // Create flow graph
    cler::BlockRunner monitor_runner(monitoring_block);
    cler::FlowGraph<cler::StdThreadPolicy, decltype(monitor_runner)> flowgraph(monitor_runner);
    
    // Start system
    flowgraph.start();
    
    // Let it run normally for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Request shutdown
    shutdown_requested = true;
    
    // Give time for shutdown to process
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    flowgraph.stop();
    
    // Verify shutdown was processed
    EXPECT_GT(cleanup_count.load(), 0);
    
    // Verify queue is in a clean state
    EXPECT_GE(shutdown_queue.size(), 0);  // Should not crash
}

// Test cross-platform compatibility markers
TEST_F(IntegrationTest, CrossPlatformCompatibility) {
    // This test verifies that all components compile and work together
    // across different platforms (the actual platform detection is tested
    // in individual component tests)
    
    // Test that we can mix stack and dynamic allocation
    dro::SPSCQueue<int, 64> stack_queue;           // Stack allocated
    dro::SPSCQueue<int> heap_queue(64);            // Heap allocated
    
#ifdef DRO_SPSC_NO_EXCEPTIONS
    ASSERT_TRUE(heap_queue.is_valid());
#endif
    
    // Test data transfer between different queue types
    for (int i = 0; i < 32; ++i) {
        stack_queue.push(i);
        
        int value;
        stack_queue.pop(value);
        heap_queue.push(value * 2);
    }
    
    // Verify final state
    EXPECT_EQ(stack_queue.size(), 0);
    EXPECT_EQ(heap_queue.size(), 32);
    
    // Clean up heap queue
    int dummy;
    while (heap_queue.try_pop(dummy)) {
        // Empty the queue
    }
    
    EXPECT_TRUE(heap_queue.empty());
}