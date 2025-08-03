#pragma once

#include "cler.hpp"
#include "liquid.h"
#include <complex>
#include <vector>
#include <deque>

extern "C" {
// #include "_ezgmsk_demod.h"
}

struct EZGmskModBlock : public cler::BlockBase {
    EZGmskModBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_symbols_len,
                   const unsigned char* syncword_symbols,
                   unsigned int syncword_symbols_len) : cler::BlockBase(name) {}

    ~EZGmskModBlock() {
        
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        if (_payload_queue.empty()) {
            return cler::Error::NotEnoughSamples;
        }
        for (const auto& payload : _payload_queue) {
            size_t required_samples = (payload.size() + header_size + preamble_size) * k;
            if (out->space() < required_samples) {
                return cler::Error::NotEnoughSpace;
            }
            //do the thing...
        }
        
        return cler::Empty{};
    }

    void send_payload(std::vector<uint8_t>&& payload) {
        _payload_queue.push_back(std::move(payload));
    }

private:
    std::deque<std::vector<uint8_t>> _payload_queue;
};
