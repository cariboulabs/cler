#pragma once

#include "cler.hpp"

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, const Mode block_mode, const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _block_mode(block_mode)
    {
        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        if (block_mode != Mode::MagPhase && block_mode != Mode::RealImag) {
            throw std::invalid_argument("Invalid block mode. Use MagPhase or RealImag.");
        }

        _tmp_c = new std::complex<float>[buffer_size];
        _tmp_a = new float[buffer_size];
        _tmp_b = new float[buffer_size];
        _buffer_size = buffer_size;
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
        size_t transferable = std::min({in.size(), a_out->space(), b_out->space(), _buffer_size});

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
