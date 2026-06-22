#pragma once

#include "cler.hpp"
#include <complex>
#include <algorithm>
#include <cmath>

// Converts complex I/Q samples to instantaneous power in dB.
//
// Backpressure-safe: it only ever processes as many samples as can fit in the
// output channel this call (min of input available, output space, scratch
// size). It never calls a blocking push() and never consumes input it cannot
// emit, so a slow downstream consumer simply throttles it instead of corrupting
// or dropping samples.
template<typename T = std::complex<float>>
struct PowerDetectorBlock : public cler::BlockBase {
    cler::Channel<T> in;

    PowerDetectorBlock(const char* name,
                       float min_power_db = -100.0f,  // Clip anything below this
                       size_t buffer_size = 32768)
        : BlockBase(name),
          in(buffer_size),
          _min_power_db(min_power_db),
          _scratch_size(buffer_size)
    {
        _in_scratch  = new T[_scratch_size];
        _out_scratch = new float[_scratch_size];
    }

    ~PowerDetectorBlock() {
        delete[] _in_scratch;
        delete[] _out_scratch;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out_base) {
        auto* out = static_cast<cler::Channel<float>*>(out_base);

        size_t transferable = std::min({in.size(), out->space(), _scratch_size});
        if (transferable == 0) {
            // Distinguish so the scheduler can back off appropriately.
            return (in.size() == 0) ? cler::Error::NotEnoughSamples
                                    : cler::Error::NotEnoughSpace;
        }

        in.readN(_in_scratch, transferable);
        for (size_t i = 0; i < transferable; ++i) {
            float re = _in_scratch[i].real();
            float im = _in_scratch[i].imag();
            float power_linear = re * re + im * im;
            float power_db = 10.0f * log10f(power_linear + 1e-20f);
            _out_scratch[i] = std::max(power_db, _min_power_db);
        }
        out->writeN(_out_scratch, transferable);

        return cler::Empty{};
    }

private:
    float  _min_power_db;
    size_t _scratch_size;
    T*     _in_scratch  = nullptr;
    float* _out_scratch = nullptr;
};
