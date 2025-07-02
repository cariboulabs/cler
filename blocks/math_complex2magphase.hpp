#pragma once

#include "cler.hpp"
#include <complex>
#include <cmath>
#include <algorithm>

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    ComplexToMagPhaseBlock(const char* name, size_t work_size)
        : cler::BlockBase(name), in(work_size), _work_size(work_size) 
    {
        if (_work_size == 0) {
            throw std::invalid_argument("Work size must be greater than zero.");
        }
        _tmp_c = new std::complex<float>[_work_size];
        _tmp_mag = new float[_work_size];
        _tmp_phase = new float[_work_size];
    }
    ~ComplexToMagPhaseBlock() {
        delete[] _tmp_c;
        delete[] _tmp_mag;
        delete[] _tmp_phase;
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::Channel<float>* mag_out,
        cler::Channel<float>* phase_out)
    {
        if (in.size() < _work_size) {
            return cler::Error::NotEnoughSamples;
        }
        if (mag_out->space() < _work_size || phase_out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        in.readN(_tmp_c, _work_size);
        for (size_t i = 0; i < _work_size; ++i) {
            _tmp_mag[i] = std::abs(_tmp_c[i]);
            _tmp_phase[i] = std::arg(_tmp_c[i]);
        }

        mag_out->writeN(_tmp_mag, _work_size);
        phase_out->writeN(_tmp_phase, _work_size);

        return cler::Empty{};
    }

private:
    size_t _work_size;
    std::complex<float>* _tmp_c;
    float* _tmp_mag;
    float* _tmp_phase;
};
