#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>
#include <vector>
#include <deque>

extern "C" {
#include "_ezgmsk_mod.h"
}

struct EZGmskModBlock : public cler::BlockBase {
    EZGmskModBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_symbols_len) 
    : cler::BlockBase(name),
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
        if (_payload_queue.empty()) {
            return cler::Error::NotEnoughSamples;
        }
        
        // Process one frame at a time
        const auto& payload = _payload_queue.front();
        
        // Assemble the frame with the payload data
        ezgmsk::ezgmsk_mod_assemble(_mod, payload.data(), payload.size());
        
        unsigned int frame_len = ezgmsk::ezgmsk_mod_get_frame_len(_mod);
        
        // Check if we have enough space for the entire frame
        if (out->space() < frame_len) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Get write pointer and generate entire frame
        auto [write_ptr, write_space] = out->write_dbf();
        if (write_space < frame_len) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Generate the complete frame
        ezgmsk::ezgmsk_mod_execute(
            _mod,
            reinterpret_cast<liquid_float_complex*>(write_ptr),
            frame_len
        );
        
        out->commit_write(frame_len);
        
        // Frame complete, remove from queue and reset
        _payload_queue.pop_front();
        ezgmsk::ezgmsk_mod_reset(_mod);
        
        return cler::Empty{};
    }

    void send_payload(std::vector<uint8_t>&& payload) {
        _payload_queue.push_back(std::move(payload));
    }

private:
    ezgmsk::ezgmsk_mod _mod = nullptr;
    unsigned int _k;
    unsigned int _m;
    float _BT;
    unsigned int _preamble_len;
    
    std::deque<std::vector<uint8_t>> _payload_queue;
};
