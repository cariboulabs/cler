#include "../include/cler_static.hpp"
#include "../include/cler_allocators.hpp"
#include <iostream>
#include <cmath>

using namespace cler;

// Example block for embedded systems - generates sine wave
template<typename T, size_t N = 0>
class SineWaveGenerator : public BlockBase {
public:
    StaticChannel<T, N> out;
    
    SineWaveGenerator(const std::string& name, T frequency, T sample_rate)
        : BlockBase(std::string(name))
        , frequency_(frequency)
        , sample_rate_(sample_rate)
        , phase_(0) {}
    
    Result<Empty, Error> procedure(ChannelBase<T>* output) {
        constexpr size_t chunk_size = 128;
        
        if (output->space() < chunk_size) {
            return Err(Error::NotEnoughSpace);
        }
        
        T buffer[chunk_size];
        const T phase_increment = 2 * M_PI * frequency_ / sample_rate_;
        
        for (size_t i = 0; i < chunk_size; ++i) {
            buffer[i] = std::sin(phase_);
            phase_ += phase_increment;
            if (phase_ >= 2 * M_PI) {
                phase_ -= 2 * M_PI;
            }
        }
        
        output->writeN(buffer, chunk_size);
        return Ok(Empty{});
    }
    
private:
    T frequency_;
    T sample_rate_;
    T phase_;
};

// FIR filter block using custom allocator for coefficients
template<typename T, size_t MaxTaps, typename CoeffAlloc = std::allocator<T>>
class FIRFilter : public BlockBase {
public:
    using value_type = T;
    
    FIRFilter(const std::string& name, const T* coeffs, size_t num_taps, 
              const CoeffAlloc& alloc = CoeffAlloc{})
        : BlockBase(std::string(name))
        , num_taps_(num_taps)
        , coeffs_alloc_(alloc) {
        
        if (num_taps > MaxTaps) {
            throw std::invalid_argument("Too many taps for static buffer");
        }
        
        // For embedded systems, we use static storage
        std::copy(coeffs, coeffs + num_taps, coeffs_.begin());
        std::fill(delay_line_.begin(), delay_line_.end(), T{0});
    }
    
    Result<Empty, Error> procedure(ChannelBase<T>* input, ChannelBase<T>* output) {
        size_t available = std::min(input->size(), output->space());
        if (available == 0) {
            return Err(Error::NotEnoughSamples);
        }
        
        // Process samples one at a time for simplicity
        for (size_t i = 0; i < available; ++i) {
            T sample;
            input->pop(sample);
            
            // Shift delay line
            for (size_t j = MaxTaps - 1; j > 0; --j) {
                delay_line_[j] = delay_line_[j - 1];
            }
            delay_line_[0] = sample;
            
            // Compute output
            T out = 0;
            for (size_t j = 0; j < num_taps_; ++j) {
                out += coeffs_[j] * delay_line_[j];
            }
            
            output->push(out);
        }
        
        return Ok(Empty{});
    }
    
private:
    size_t num_taps_;
    std::array<T, MaxTaps> coeffs_;
    std::array<T, MaxTaps> delay_line_;
    CoeffAlloc coeffs_alloc_;
};

// Sink block that accumulates statistics
template<typename T>
class StatisticsSink : public BlockBase {
public:
    StatisticsSink(const std::string& name)
        : BlockBase(std::string(name))
        , count_(0)
        , sum_(0)
        , sum_sq_(0) {}
    
    Result<Empty, Error> procedure(ChannelBase<T>* input) {
        size_t available = input->size();
        if (available == 0) {
            return Err(Error::NotEnoughSamples);
        }
        
        T buffer[256];
        size_t read = input->readN(buffer, std::min(available, size_t(256)));
        
        for (size_t i = 0; i < read; ++i) {
            sum_ += buffer[i];
            sum_sq_ += buffer[i] * buffer[i];
            count_++;
        }
        
        return Ok(Empty{});
    }
    
    T mean() const { return count_ > 0 ? sum_ / count_ : 0; }
    T variance() const {
        if (count_ <= 1) return 0;
        T m = mean();
        return (sum_sq_ / count_) - (m * m);
    }
    size_t count() const { return count_; }
    
private:
    size_t count_;
    T sum_;
    T sum_sq_;
};

int main() {
    std::cout << "CLER Embedded Static Example\n";
    std::cout << "============================\n\n";
    
    // Use static channels with compile-time sizes
    constexpr size_t channel_size = 1024;
    using SampleType = float;
    
    // Create blocks
    SineWaveGenerator<SampleType, channel_size> sine_gen("SineGen", 440.0f, 48000.0f);
    
    // Create FIR filter with custom allocator
    constexpr size_t max_taps = 32;
    using FilterAlloc = PoolAllocator<SampleType, sizeof(SampleType) * max_taps, 4>;
    
    // Simple low-pass filter coefficients
    SampleType coeffs[] = {0.25f, 0.5f, 0.25f};
    FIRFilter<SampleType, max_taps, FilterAlloc> filter("LowPass", coeffs, 3);
    
    StatisticsSink<SampleType> sink("Stats");
    
    // Static channels
    StaticChannel<SampleType, channel_size> ch1;
    StaticChannel<SampleType, channel_size> ch2;
    
    // Create static flowgraph
    StaticFlowGraph flowgraph(
        BlockRunner(&sine_gen, &ch1),
        BlockRunner(&filter, &ch1, &ch2),
        BlockRunner(&sink, &ch2)
    );
    
    std::cout << "Running static flowgraph for embedded system...\n";
    
    // Simulate RTOS task creation
    FreeRTOSTaskFactory task_factory;
    flowgraph.run_with_tasks(task_factory);
    
    // Run for a short time
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    flowgraph.stop();
    
    std::cout << "\nStatistics:\n";
    std::cout << "Samples processed: " << sink.count() << "\n";
    std::cout << "Mean: " << sink.mean() << "\n";
    std::cout << "Variance: " << sink.variance() << "\n";
    
    // Demonstrate memory pool allocator
    std::cout << "\nMemory Pool Allocator Demo:\n";
    MemoryPoolAllocator<64, 10> pool;
    
    void* blocks[5];
    for (int i = 0; i < 5; ++i) {
        blocks[i] = pool.allocate(32);
        std::cout << "Allocated block " << i << ": " 
                  << (blocks[i] ? "success" : "failed") << "\n";
    }
    
    // Free some blocks
    pool.deallocate(blocks[1], 32);
    pool.deallocate(blocks[3], 32);
    
    // Allocate again to show reuse
    void* reused = pool.allocate(32);
    std::cout << "Reused block: " << (reused ? "success" : "failed") << "\n";
    
    return 0;
}