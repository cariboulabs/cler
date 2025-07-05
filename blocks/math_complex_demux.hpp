#pragma once

#include "cler.hpp"
#include <complex>
#include <cmath>
#include <algorithm>

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, Mode block_mode)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE), _block_mode(block_mode)
    {
        if (cler::DEFAULT_BUFFER_SIZE == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }

        _tmp_c = new std::complex<float>[cler::DEFAULT_BUFFER_SIZE];
        _tmp_a = new float[cler::DEFAULT_BUFFER_SIZE];
        _tmp_b = new float[cler::DEFAULT_BUFFER_SIZE];
    }
    ~ComplexToMagPhaseBlock() {
        delete[] _tmp_c;
        delete[] _tmp_a;
        delete[] _tmp_b;
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* a_out,
        cler::ChannelBase<float>* b_out)
    {
        size_t transferable = cler::floor2(std::min({in.size(), a_out->space(), b_out->space(), cler::DEFAULT_BUFFER_SIZE}));

        if (transferable == 0) {
            return cler::Error::NotEnoughSamples;
        }

        in.readN(_tmp_c, transferable);
        for (size_t i = 0; i < transferable; ++i) {
            switch (_block_mode) {
                case Mode::MagPhase:
                    _tmp_a[i] = std::abs(_tmp_c[i]);
                    _tmp_b[i] = std::arg(_tmp_c[i]);
                    break;
                case Mode::RealImag:
                    _tmp_a[i] = _tmp_c[i].real();
                    _tmp_b[i] = _tmp_c[i].imag();
                    break;
            }
        }

        a_out->writeN(_tmp_a, transferable);
        b_out->writeN(_tmp_b, transferable);

        return cler::Empty{};
    }

private:
    Mode _block_mode;
    size_t _buffer_size;
    std::complex<float>* _tmp_c;
    float* _tmp_a;
    float* _tmp_b;
};
