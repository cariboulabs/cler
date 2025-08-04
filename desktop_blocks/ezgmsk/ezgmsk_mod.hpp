#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <vector>
#include "../blob.hpp"

extern "C" {
#include "_ezgmsk_mod.h"
}

struct EZGmskModBlock : public cler::BlockBase {
    cler::Channel<Blob> in;

    EZGmskModBlock(const char* name,
                unsigned int k,
                unsigned int m,
                float BT,
                unsigned int preamble_symbols_len,
                const size_t buffer_size = 512) 
    : cler::BlockBase(name),
        in(buffer_size),
        _k(k),
        _m(m),
        _BT(BT),
        _preamble_len(preamble_symbols_len)
    {
        // Create the modulator
        _mod = ezgmsk::ezgmsk_mod_create_set(k, m, BT, preamble_symbols_len);
        
        if (!_mod) {
            throw std::runtime_error("Failed to create EZGMSK modulator");
        }
    }

    ~EZGmskModBlock() {
        if (_mod) {
            ezgmsk::ezgmsk_mod_destroy(_mod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        const Blob* ptr1, *ptr2;
        size_t size1, size2;
        size_t available = in.peek_read(ptr1, size1, ptr2, size2);
        
        for (size_t i = 0; i < available; ++i) {
            Blob* blob = const_cast<Blob*>(
                (i < size1) ? (ptr1 + i) : (ptr2 + i - size1)
            );
            ezgmsk::ezgmsk_mod_assemble(_mod, blob->data, blob->len);

            unsigned int frame_len = ezgmsk::ezgmsk_mod_get_frame_len(_mod);
            auto [write_ptr, write_space] = out->write_dbf();

            if (write_space < frame_len * sizeof(liquid_float_complex)) {
                break;
            }

            ezgmsk::ezgmsk_mod_execute(
            _mod,
            reinterpret_cast<liquid_float_complex*>(write_ptr),
            frame_len
            );

            out->commit_write(frame_len);
            ezgmsk::ezgmsk_mod_reset(_mod);

            blob->release();
            in.commit_read(1); // Commit the read of the blob
        }
        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_mod _mod = nullptr;
    unsigned int _k;
    unsigned int _m;
    float _BT;
    unsigned int _preamble_len;
};
