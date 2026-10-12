#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/bpsk/bpsk_common.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

// Packets in as length-prefixed records ([length][payload], length 1..64), +-1 BPSK
// symbols out (frame layout in bpsk_common.hpp). A record with an invalid length is
// consumed and counted, never framed.
//
// One frame is in flight at a time: a record is consumed only when the previous
// frame has been fully written out, so backpressure never truncates a frame.
// The first frame is preceded by `lead_in_symbols` of a fixed +-1 pattern so the
// receiver's timing and carrier loops can settle before the first preamble. With
// `idle_fill`, the same pattern is sent whenever no record is waiting, so a
// radio keeps streaming, the receiver stays locked, and its one-block latency
// never holds the last frame. The pattern's normalised correlation with the
// preamble is at most 0.54, below the deframer's threshold.
struct BPSKFramerBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    BPSKFramerBlock(const char* name, size_t lead_in_symbols = 128, bool idle_fill = false,
        size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _lead_in(lead_in_symbols), _idle_fill(idle_fill) {
        if (lead_in_symbols > MAX_LEAD_IN) {
            cler::panic("BPSKFramerBlock lead-in longer than 1024 symbols");
        }
        if (buffer_size < 1 + bpsk::MAX_PAYLOAD) {
            cler::panic("BPSKFramerBlock input buffer smaller than one record");
        }
        if (buffer_size * sizeof(uint8_t) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    uint64_t frames_sent() const { return _frames.load(std::memory_order_relaxed); }
    uint64_t records_rejected() const { return _rejected.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (_pos == _len) {
            auto [rptr, rsize] = in.read_dbf();
            const size_t length = rsize > 0 ? rptr[0] : 0;
            if (rsize > 0 && (length == 0 || length > bpsk::MAX_PAYLOAD)) {
                in.commit_read(1);
                _rejected.fetch_add(1, std::memory_order_relaxed);
                return cler::Empty{};
            }
            if (rsize == 0 || rsize < 1 + length) {
                if (!_idle_fill) return cler::Error::NotEnoughSamples;
                if (out->space() == 0) return cler::Error::NotEnoughSpace;
                build_idle();
            } else {
                // Consume the record only once the first symbols can leave.
                if (out->space() == 0) return cler::Error::NotEnoughSpace;
                build(rptr, length);
                in.commit_read(1 + length);
            }
        }

        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;
        const size_t n = std::min(wsize, _len - _pos);
        std::copy_n(_frame.data() + _pos, n, wptr);
        out->commit_write(n);
        _pos += n;
        if (_pos == _len && !_idle_frame) _frames.fetch_add(1, std::memory_order_relaxed);
        return cler::Empty{};
    }

private:
    // Lead-in, then the frame proper; record = [length][payload].
    void build(const uint8_t* record, size_t length) {
        _len = 0;
        _idle_frame = false;
        for (size_t i = 0; i < _lead_in; ++i) _frame[_len++] = idle_symbol();
        _lead_in = 0;
        for (float s : bpsk::PREAMBLE) _frame[_len++] = s;
        const uint16_t crc = bpsk::crc16_ccitt(record, 1 + length);
        for (size_t i = 0; i < 1 + length; ++i) push_byte(record[i]);
        push_byte(static_cast<uint8_t>(crc >> 8));
        push_byte(static_cast<uint8_t>(crc & 0xFF));
        for (size_t i = 0; i < bpsk::GUARD_SYMBOLS; ++i) _frame[_len++] = 0.0f;
        _pos = 0;
    }

    void build_idle() {
        _len = 0;
        _idle_frame = true;
        for (size_t i = 0; i < IDLE_CHUNK; ++i) _frame[_len++] = idle_symbol();
        _pos = 0;
    }

    // x^7 + x^6 + 1, a fixed pattern with plenty of transitions for the TED.
    float idle_symbol() {
        const uint32_t bit = ((_lfsr >> 6) ^ (_lfsr >> 5)) & 1u;
        _lfsr = ((_lfsr << 1) | bit) & 0x7Fu;
        return bit ? -1.0f : 1.0f;
    }

    void push_byte(uint8_t b) {
        for (int bit = 7; bit >= 0; --bit) _frame[_len++] = ((b >> bit) & 1u) ? -1.0f : 1.0f;
    }

    static constexpr size_t MAX_LEAD_IN = 1024;
    static constexpr size_t IDLE_CHUNK = 64;

    size_t _lead_in;
    bool _idle_fill;
    bool _idle_frame = false;
    uint32_t _lfsr = 0x1u;
    std::array<float, MAX_LEAD_IN + bpsk::MAX_FRAME_SYMBOLS> _frame{};
    size_t _len = 0;
    size_t _pos = 0;
    std::atomic<uint64_t> _frames{0};
    std::atomic<uint64_t> _rejected{0};
};
