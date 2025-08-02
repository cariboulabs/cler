#pragma once

#include "cler.hpp"

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, const Mode block_mode, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _block_mode(block_mode)
    {
        // Calculate proper buffer size for complex<float>
        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size;
        
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) + " complex<float> elements");
        }
        if (block_mode != Mode::MagPhase && block_mode != Mode::RealImag) {
            throw std::invalid_argument("Invalid block mode. Use MagPhase or RealImag.");
        }

        try {
            _tmp_c = new std::complex<float>[actual_buffer_size];
        } catch (const std::bad_alloc&) {
            throw std::runtime_error("Failed to allocate complex buffer");
        }
        
        try {
            _tmp_a = new float[actual_buffer_size];
        } catch (const std::bad_alloc&) {
            delete[] _tmp_c;
            _tmp_c = nullptr;
            throw std::runtime_error("Failed to allocate first output buffer");
        }
        
        try {
            _tmp_b = new float[actual_buffer_size];
        } catch (const std::bad_alloc&) {
            delete[] _tmp_a;
            _tmp_a = nullptr;
            delete[] _tmp_c;
            _tmp_c = nullptr;
            throw std::runtime_error("Failed to allocate second output buffer");
        }
        _buffer_size = actual_buffer_size;
    }
    ~ComplexToMagPhaseBlock() {
        if (_tmp_c) {
            delete[] _tmp_c;
        }
        if (_tmp_a) {
            delete[] _tmp_a;
        }
        if (_tmp_b) {
            delete[] _tmp_b;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* a_out,
        cler::ChannelBase<float>* b_out)
    {
        // Use readN/writeN for simple processing (recommended pattern)
        size_t transferable = std::min({in.size(), a_out->space(), b_out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSamples;
        
        // Read input data
        in.readN(_tmp_c, transferable);
        
        // Process data based on mode
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
        
        // Write output data
        a_out->writeN(_tmp_a, transferable);
        b_out->writeN(_tmp_b, transferable);
        
        return cler::Empty{};
    }

private:
    Mode _block_mode;
    size_t _buffer_size;
    std::complex<float>* _tmp_c = nullptr;
    float* _tmp_a = nullptr;
    float* _tmp_b = nullptr;

};
