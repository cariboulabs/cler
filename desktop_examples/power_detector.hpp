#pragma once

#include "cler.hpp"
#include <complex>
#include <algorithm>

// Converts complex I/Q samples to instantaneous power in dB
template<typename T = std::complex<float>>
struct PowerDetectorBlock : public cler::BlockBase {
    cler::Channel<T> in;

    PowerDetectorBlock(const char* name, 
                       float min_power_db = -100.0f,  // Clip anything below this
                       size_t buffer_size = 32768)
        : BlockBase(name),
          in(buffer_size),
          _min_power_db(min_power_db)
    {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out_base) {
        // Cast to the actual Channel type
        auto* out = static_cast<cler::Channel<float>*>(out_base);
        
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Process all available samples
        T sample;
        for (size_t i = 0; i < available; ++i) {
            in.pop(sample);
            float re = sample.real();
            float im = sample.imag();
            float power_linear = re * re + im * im;
            float power_db = 10.0f * log10f(power_linear + 1e-20f);
            
            // Clip to minimum power threshold
            power_db = std::max(power_db, _min_power_db);
            
            out->push(power_db);
        }

        return cler::Empty{};
    }

private:
    float _min_power_db;
};