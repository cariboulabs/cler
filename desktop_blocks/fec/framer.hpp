#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// Packets in, complex baseband out, using liquid's flexframegen: preamble,
// coded header (CRC + FEC), then a coded payload of `packet_bytes`. Modulation,
// CRC and the two FEC layers are the flexframegen properties.
//
// One frame is in flight at a time: a packet is consumed only when the previous
// frame has been fully written out, so backpressure never truncates a frame.
struct PacketFramerBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    PacketFramerBlock(const char* name,
                      size_t packet_bytes,
                      modulation_scheme scheme = LIQUID_MODEM_QPSK,
                      crc_scheme check = LIQUID_CRC_32,
                      fec_scheme fec0 = LIQUID_FEC_NONE,
                      fec_scheme fec1 = LIQUID_FEC_HAMMING128,
                      size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _packet_bytes(packet_bytes) {
        if (packet_bytes == 0 || packet_bytes > LIQUID_MAX_PAYLOAD_LEN) {
            cler::panic("PacketFramerBlock: packet_bytes out of range");
        }
        if (buffer_size < packet_bytes) {
            cler::panic("PacketFramerBlock input buffer smaller than one packet");
        }
        flexframegenprops_s props;
        flexframegenprops_init_default(&props);
        props.check = check;
        props.fec0 = fec0;
        props.fec1 = fec1;
        props.mod_scheme = scheme;
        _fg = flexframegen_create(&props);
        if (!_fg) {
            cler::panic("PacketFramerBlock: flexframegen_create failed (unsupported scheme/fec)");
        }
        _payload.resize(_packet_bytes);
        _samples.resize(CHUNK);
        std::memset(_header, 0, sizeof(_header));
    }

    ~PacketFramerBlock() { flexframegen_destroy(_fg); }

    size_t packet_bytes() const { return _packet_bytes; }

    // Samples per frame; valid once a packet has been assembled.
    unsigned int frame_samples() const { return _frame_samples; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        bool progress = false;
        if (_remaining == 0) {
            if (in.size() < _packet_bytes) {
                return cler::Error::NotEnoughSamples;
            }
            in.readN(_payload.data(), _packet_bytes);
            flexframegen_assemble(_fg, _header, _payload.data(), static_cast<unsigned int>(_packet_bytes));
            _frame_samples = flexframegen_getframelen(_fg);
            _remaining = _frame_samples;
            progress = true;
        }
        const size_t n = std::min({_remaining, out->space(), _samples.size()});
        if (n == 0) {
            return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                            : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSpace);
        }
        flexframegen_write_samples(_fg, _samples.data(), static_cast<unsigned int>(n));
        out->writeN(_samples.data(), n);
        _remaining -= n;
        return cler::Empty{};
    }

private:
    static constexpr size_t CHUNK = 4096;

    size_t _packet_bytes;
    size_t _remaining = 0;
    unsigned int _frame_samples = 0;
    flexframegen _fg = nullptr;
    unsigned char _header[8];
    std::vector<uint8_t> _payload;
    std::vector<std::complex<float>> _samples;
};
