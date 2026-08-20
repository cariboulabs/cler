#pragma once

#include "cler.hpp"
#include "desktop_blocks/fec/fec.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Inverse of FECEncoderBlock: consumes one coded block of
// fec_get_enc_msg_length(scheme, payload_bytes) bytes and emits payload_bytes.
//
// liquid's fec_decode() reports no uncorrectable-error indication for the block
// codes, so a codeword corrupted beyond the code's capability decodes silently
// to the wrong payload; detection belongs to a CRC above this block.
struct FECDecoderBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    FECDecoderBlock(const char* name, size_t payload_bytes, fec_scheme scheme, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _payload_bytes(payload_bytes) {
        if (payload_bytes == 0) {
            cler::panic("FECDecoderBlock requires payload_bytes > 0");
        }
        _fec = fec_create_or_panic(scheme, name);
        _encoded_bytes = fec_get_enc_msg_length(scheme, static_cast<unsigned int>(payload_bytes));
        if (buffer_size < _encoded_bytes) {
            cler::panic("FECDecoderBlock input buffer smaller than one encoded block");
        }
        _dec.resize(_payload_bytes);
        _enc.resize(_encoded_bytes);
    }

    ~FECDecoderBlock() { fec_destroy(_fec); }

    size_t payload_bytes() const { return _payload_bytes; }
    size_t encoded_bytes() const { return _encoded_bytes; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t blocks = std::min(in.size() / _encoded_bytes, out->space() / _payload_bytes);
        if (blocks == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        for (size_t b = 0; b < blocks; ++b) {
            in.readN(_enc.data(), _encoded_bytes);
            fec_decode(_fec, static_cast<unsigned int>(_payload_bytes), _enc.data(), _dec.data());
            out->writeN(_dec.data(), _payload_bytes);
        }
        return cler::Empty{};
    }

private:
    size_t _payload_bytes;
    size_t _encoded_bytes = 0;
    fec _fec = nullptr;
    std::vector<uint8_t> _dec, _enc;
};
