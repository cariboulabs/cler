#pragma once

#include "cler.hpp"

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, const Mode block_mode, const size_t buffer_size = 512)
        : cler::BlockBase(name), in(buffer_size), _block_mode(block_mode) // Default 512 for complex<float> = 4KB
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
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [a_write_ptr, a_write_size] = a_out->write_dbf();
        auto [b_write_ptr, b_write_size] = b_out->write_dbf();
        
        size_t to_process = std::min({read_size, a_write_size, b_write_size});
        
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                switch (_block_mode) {
                    case Mode::MagPhase:
                        a_write_ptr[i] = std::abs(read_ptr[i]);
                        b_write_ptr[i] = std::arg(read_ptr[i]);
                        break;
                    case Mode::RealImag:
                        a_write_ptr[i] = read_ptr[i].real();
                        b_write_ptr[i] = read_ptr[i].imag();
                        break;
                }
            }
            
            in.commit_read(to_process);
            a_out->commit_write(to_process);
            b_out->commit_write(to_process);
        }
        return cler::Empty{};
    }

private:
    Mode _block_mode;
    size_t _buffer_size;
    std::complex<float>* _tmp_c;
    float* _tmp_a;
    float* _tmp_b;

};
