#pragma once

#include "cler.hpp"
#include "desktop_blocks/fec/fec.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Block FEC encoder: consumes `payload_bytes` at a time and emits
// fec_get_enc_msg_length(scheme, payload_bytes) coded bytes. A whole block is
// consumed only when the whole coded block fits, so a short output never splits
// a codeword.
struct FECEncoderBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    FECEncoderBlock(const char* name, size_t payload_bytes, fec_scheme scheme, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _payload_bytes(payload_bytes) {
        if (payload_bytes == 0) {
            cler::panic("FECEncoderBlock requires payload_bytes > 0");
        }
        _fec = fec_create_or_panic(scheme, name);
        _encoded_bytes = fec_get_enc_msg_length(scheme, static_cast<unsigned int>(payload_bytes));
        if (buffer_size < payload_bytes) {
            cler::panic("FECEncoderBlock input buffer smaller than one payload block");
        }
        _dec.resize(_payload_bytes);
        _enc.resize(_encoded_bytes);
    }

    ~FECEncoderBlock() { fec_destroy(_fec); }

    size_t payload_bytes() const { return _payload_bytes; }
    size_t encoded_bytes() const { return _encoded_bytes; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t blocks = std::min(in.size() / _payload_bytes, out->space() / _encoded_bytes);
        if (blocks == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        for (size_t b = 0; b < blocks; ++b) {
            in.readN(_dec.data(), _payload_bytes);
            fec_encode(_fec, static_cast<unsigned int>(_payload_bytes), _dec.data(), _enc.data());
            out->writeN(_enc.data(), _encoded_bytes);
        }
        return cler::Empty{};
    }

private:
    size_t _payload_bytes;
    size_t _encoded_bytes = 0;
    fec _fec = nullptr;
    std::vector<uint8_t> _dec, _enc;
};
