#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>
#include <iostream>

// Include your demod header if not already done
extern "C" {
#include "_ezgmsk_demod.h"
}

struct GmskDemodBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;   // Complex baseband input samples
    cler::Channel<unsigned char> out;        // Output payload bytes

    GmskDemodBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_len,
                   const unsigned char* syncword,
                   unsigned int syncword_len,
                   unsigned int header_symbols_len,
                   float detector_threshold,
                   float detector_dphi_max,
                   unsigned int payload_max_bytes_len,
                   ezgmsk_demod_callback callback,
                   void* callback_context)
    : BlockBase(name),
      in(cler::DEFAULT_BUFFER_SIZE)
    {
        // Create demod
        _demod = ezgmsk_demod_create_set(
            k, m, BT,
            preamble_len,
            syncword, syncword_len,
            header_symbols_len,
            detector_threshold,
            detector_dphi_max,
            payload_max_bytes_len,
            callback,
            callback_context
        );

        if (!_demod) {
            throw std::runtime_error("Failed to create ezgmsk_demod");
        }
    }

    ~GmskDemodBlock() {
        if (_demod) {
            ezgmsk_demod_destroy(_demod);
            _demod = nullptr;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Determine how many samples we can feed
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Prepare buffer for samples to pass to demod
        std::vector<liquid_float_complex> buffer(available);

        for (size_t i = 0; i < available; ++i) {
            std::complex<float> sample;
            in.pop(sample);
            buffer[i] = liquid_float_complex{sample.real(), sample.imag()};
        }

        // Call demodulator
        int result = ezgmsk_demod_execute(_demod, buffer.data(), buffer.size());
        if (result != 0) {
            std::cerr << "GMSK demod execution error!" << std::endl;
            return cler::Error::ProcedureError;
        }

        return cler::Empty{};
    }

private:
    ezgmsk_demod _demod = nullptr;
};
