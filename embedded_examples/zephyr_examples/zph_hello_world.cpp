#include "cler.hpp"
#include "task_policies/cler_zephyr_tpolicy.hpp"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

// Simple embedded blocks for Zephyr
template <typename T>
struct EmbeddedSourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "EmbeddedSourceCWBlock only supports float or std::complex<float>");

    EmbeddedSourceCWBlock(const char* name,
                          float amplitude,
                          float frequency_hz,
                          size_t sps,
                          size_t buffer_size = 128)  // Smaller buffer for embedded
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps),
          _buffer_size(buffer_size)
    {
        if (_sps == 0) {
            printk("Error: Sample rate must be greater than zero\n");
            return;
        }

        float phase_increment = 2.0f * 3.14159f * _frequency_hz / static_cast<float>(_sps);
        _phasor = std::complex<float>(1.0f, 0.0f);
        _phasor_inc = std::complex<float>(cosf(phase_increment), sinf(phase_increment));
        
        // Static allocation for embedded
        _sample_count = 0;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_generate = std::min(available_space, _buffer_size);
        
        for (size_t i = 0; i < to_generate; ++i) {
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
    size_t _buffer_size;
    size_t _sample_count;

    std::complex<float> _phasor = {1.0f, 0.0f};
    std::complex<float> _phasor_inc = {1.0f, 0.0f};
};

template <typename T>
struct EmbeddedAddBlock : public cler::BlockBase {
    cler::Channel<T, 128> in1;  // Stack allocated
    cler::Channel<T, 128> in2;  // Stack allocated

    EmbeddedAddBlock(const char* name)
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
struct EmbeddedPrintSinkBlock : public cler::BlockBase {
    cler::Channel<T, 128> in;  // Stack allocated

    EmbeddedPrintSinkBlock(const char* name)
        : cler::BlockBase(name), _sample_count(0) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available_samples = in.size();
        
        for (size_t i = 0; i < available_samples; ++i) {
            T sample;
            in.pop(sample);
            _sample_count++;
            
            // Print every 100th sample to avoid spamming
            if (_sample_count % 100 == 0) {
                if constexpr (std::is_same_v<T, float>) {
                    printk("Sample %zu: %d.%03d\n", _sample_count, 
                           (int)sample, (int)((sample - (int)sample) * 1000));
                } else {
                    printk("Sample %zu: complex value\n", _sample_count);
                }
            }
        }

        return cler::Empty{};
    }

private:
    size_t _sample_count;
};

int main() {
    printk("CLER Zephyr Hello World Example\n");
    printk("Starting DSP flowgraph...\n");

    const size_t SPS = 1000;
    
    EmbeddedSourceCWBlock<float> source1("CWSource1", 1.0f, 1.0f, SPS, 64);
    EmbeddedSourceCWBlock<float> source2("CWSource2", 0.5f, 10.0f, SPS, 64);
    EmbeddedAddBlock<float> adder("Adder");
    EmbeddedPrintSinkBlock<float> sink("PrintSink");

    // Create Zephyr flowgraph
    auto flowgraph = cler::make_zephyr_flowgraph(
        cler::BlockRunner(&source1, &adder.in1),
        cler::BlockRunner(&source2, &adder.in2),
        cler::BlockRunner(&adder, &sink.in),
        cler::BlockRunner(&sink)
    );
    
    flowgraph.run();

    return 0;
}