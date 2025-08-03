#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>

extern "C" {
// #include "_ezgmsk_demod.h"
}

struct EZGmskModBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    EZGmskModBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_symbols_len,
                   const unsigned char* syncword_symbols,
                   unsigned int syncword_symbols_len,
                   unsigned int header_bytes_len,
                   unsigned int payload_max_bytes_len,
                //    ezgmsk::ezgmsk_demod_callback callback,
                   void* callback_context,
                   size_t buffer_size = 0)
    : BlockBase(name),
      in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size)
    {
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) + " complex<float> elements");
        }
    }

    ~EZGmskModBlock() {
        if (_mod) {
            ezgmsk::ezgmsk_mod_destroy(_mod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        // Process directly from doubly-mapped buffer
        // Note: liquid DSP functions don't modify input, so const_cast is safe here
        ezgmsk::ezgmsk_demod_execute(_demod, 
            reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)), 
            read_size);
        in.commit_read(read_size);
        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_demod _demod = nullptr;
};
