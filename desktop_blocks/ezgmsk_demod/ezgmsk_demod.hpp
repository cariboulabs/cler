#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>

extern "C" {
#include "_ezgmsk_demod.h"
}

struct EZGmskDemodBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    EZGmskDemodBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_symbols_len,
                   const unsigned char* syncword_symbols,
                   unsigned int syncword_symbols_len,
                   unsigned int header_bytes_len,
                   unsigned int payload_max_bytes_len,
                   ezgmsk::ezgmsk_demod_callback callback,
                   void* callback_context,
                   float detector_threshold = 0.9f,
                   float detector_dphi_max = 0.1f)
    : BlockBase(name),
      in(512) // 512 * 8 bytes (complex<float>) = 4KB
    {
        _demod = ezgmsk_demod_create_set(
            k, m, BT,
            preamble_symbols_len,
            syncword_symbols,
            syncword_symbols_len,
            header_bytes_len,
            payload_max_bytes_len,
            detector_threshold,
            detector_dphi_max,
            callback,
            callback_context
        );
    }

    ~EZGmskDemodBlock() {
        if (_demod) {
            ezgmsk::ezgmsk_demod_destroy(_demod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        
        if (read_size > 0) {
            // Process directly from doubly-mapped buffer
            // Note: liquid DSP functions don't modify input, so const_cast is safe here
            ezgmsk::ezgmsk_demod_execute(_demod, 
                reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)), 
                read_size);
            in.commit_read(read_size);
        }
        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_demod _demod = nullptr;
};
