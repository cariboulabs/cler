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
      in(cler::DEFAULT_BUFFER_SIZE)
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

        _tmp = new std::complex<float>[cler::DEFAULT_BUFFER_SIZE];
    }

    ~EZGmskDemodBlock() {
        if (_demod) {
            ezgmsk::ezgmsk_demod_destroy(_demod);
        }
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        in.readN(_tmp, available);
        ezgmsk::ezgmsk_demod_execute(_demod, reinterpret_cast<liquid_float_complex*>(_tmp), available);

        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_demod _demod = nullptr;
    std::complex<float>* _tmp;
};
