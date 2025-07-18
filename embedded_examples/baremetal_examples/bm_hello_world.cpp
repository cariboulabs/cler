#include "cler.hpp"
#include <stdio.h>

// Bare metal blocks using streamlined approach (no flowgraph)
template <typename T>
struct BaremetalSourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "BaremetalSourceCWBlock only supports float or std::complex<float>");

    BaremetalSourceCWBlock(const char* name,
                           float amplitude,
                           float frequency_hz,
                           size_t sps)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps)
    {
        if (_sps == 0) {
            printf("Error: Sample rate must be greater than zero\n");
            return;
        }

        float phase_increment = 2.0f * 3.14159f * _frequency_hz / static_cast<float>(_sps);
        _phasor = std::complex<float>(1.0f, 0.0f);
        _phasor_inc = std::complex<float>(cosf(phase_increment), sinf(phase_increment));
        
        _sample_count = 0;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Generate samples one by one for simplicity
        for (size_t i = 0; i < available_space; ++i) {
            std::complex<float> cw = _phasor;

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                T sample = _amplitude * cw;
                out->push(sample);
            } else {
                T sample = _amplitude * cw.real();
                out->push(sample);
            }

            _phasor *= _phasor_inc;
            // Periodic normalization for stability
            if (++_sample_count % 100 == 0) {
                float mag = std::abs(_phasor);
                if (mag > 0.0f) {
                    _phasor /= mag;
                }
            }
        }

        return cler::Empty{};
    }

private:
    float _amplitude;
    float _frequency_hz;
    size_t _sps;
    size_t _sample_count;

    std::complex<float> _phasor = {1.0f, 0.0f};
    std::complex<float> _phasor_inc = {1.0f, 0.0f};
};

template <typename T>
struct BaremetalAddBlock : public cler::BlockBase {
    cler::Channel<T, 64> in1;  // Stack allocated
    cler::Channel<T, 64> in2;  // Stack allocated

    BaremetalAddBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t min_samples = std::min(in1.size(), in2.size());
        if (min_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t to_process = std::min({available_space, min_samples});
        
        for (size_t i = 0; i < to_process; ++i) {
            T val1, val2;
            in1.pop(val1);
            in2.pop(val2);
            out->push(val1 + val2);
        }

        return cler::Empty{};
    }
};

template <typename T>
struct BaremetalPrintSinkBlock : public cler::BlockBase {
    cler::Channel<T, 64> in;  // Stack allocated

    BaremetalPrintSinkBlock(const char* name)
        : cler::BlockBase(name), _sample_count(0) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available_samples = in.size();
        
        for (size_t i = 0; i < available_samples; ++i) {
            T sample;
            in.pop(sample);
            _sample_count++;
            
            // Print every 50th sample to avoid spamming in bare metal
            if (_sample_count % 50 == 0) {
                if constexpr (std::is_same_v<T, float>) {
                    printf("Sample %zu: %.3f\n", _sample_count, sample);
                } else {
                    printf("Sample %zu: complex value\n", _sample_count);
                }
            }
        }

        return cler::Empty{};
    }

private:
    size_t _sample_count;
};

// Simple timing function for bare metal
volatile size_t tick_counter = 0;

void simple_delay_ms(size_t ms) {
    // Simple busy wait delay - in real bare metal, you'd use hardware timers
    volatile size_t count = ms * 1000;
    while (count--) {
        // Busy wait
    }
}

int main() {
    printf("CLER Bare Metal Hello World Example\n");
    printf("Using streamlined approach (no flowgraph)\n");

    const size_t SPS = 1000;
    
    // Create blocks
    BaremetalSourceCWBlock<float> source1("CWSource1", 1.0f, 1.0f, SPS);
    BaremetalSourceCWBlock<float> source2("CWSource2", 0.5f, 10.0f, SPS);
    BaremetalAddBlock<float> adder("Adder");
    BaremetalPrintSinkBlock<float> sink("PrintSink");

    printf("Running streamlined DSP chain for 1000 iterations...\n");
    
    // Streamlined execution loop - no threading, no flowgraph
    cler::Result<cler::Empty, cler::Error> result = cler::Empty{};
    
    for (size_t iteration = 0; iteration < 1000; ++iteration) {
        // Execute the DSP chain step by step
        result = source1.procedure(&adder.in1);
        if (result.is_err()) {
            printf("Source1 error: %s\n", cler::to_str(result.unwrap_err()));
            break;
        }
        
        result = source2.procedure(&adder.in2);
        if (result.is_err()) {
            printf("Source2 error: %s\n", cler::to_str(result.unwrap_err()));
            break;
        }
        
        result = adder.procedure(&sink.in);
        if (result.is_err()) {
            printf("Adder error: %s\n", cler::to_str(result.unwrap_err()));
            break;
        }
        
        result = sink.procedure();
        if (result.is_err()) {
            printf("Sink error: %s\n", cler::to_str(result.unwrap_err()));
            break;
        }
        
        // Simple delay to avoid overwhelming the system
        if (iteration % 10 == 0) {
            simple_delay_ms(1);
        }
    }
    
    printf("Completed bare metal DSP processing!\n");
    printf("Hello World from Bare Metal CLER!\n");

    return 0;
}