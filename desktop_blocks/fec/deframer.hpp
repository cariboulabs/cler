#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// Complex baseband in, recovered packets out (inverse of PacketFramerBlock).
//
// flexframesync reports a frame through a C callback, which can fire several
// times inside one execute() call, so payloads land in a preallocated staging
// ring that procedure() drains into the output channel. A payload is accepted
// only when the CRC passed and its length matches packet_bytes: the length comes
// out of a header that noise can corrupt.
struct PacketDeframerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    PacketDeframerBlock(const char* name,
                        size_t packet_bytes,
                        size_t buffer_size = 8192)
        : cler::BlockBase(name), in(buffer_size), _packet_bytes(packet_bytes),
          _staging(packet_bytes * STAGING_PACKETS) {
        if (packet_bytes == 0 || packet_bytes > LIQUID_MAX_PAYLOAD_LEN) {
            cler::panic("PacketDeframerBlock: packet_bytes out of range");
        }
        _fs = flexframesync_create(&PacketDeframerBlock::on_frame, this);
        if (!_fs) {
            cler::panic("PacketDeframerBlock: flexframesync_create failed");
        }
        _samples.resize(CHUNK);
        _drain.resize(CHUNK);
    }

    ~PacketDeframerBlock() { flexframesync_destroy(_fs); }

    size_t packet_bytes() const { return _packet_bytes; }

    unsigned int frames_detected() const { return flexframesync_get_framedatastats(_fs).num_frames_detected; }
    unsigned int headers_valid() const { return flexframesync_get_framedatastats(_fs).num_headers_valid; }
    unsigned int payloads_valid() const { return flexframesync_get_framedatastats(_fs).num_payloads_valid; }
    // Payloads that passed the CRC but found no staging room; should stay zero.
    uint64_t payloads_dropped() const { return _dropped.load(std::memory_order_relaxed); }

    // Signal quality of the last accepted payload; safe to read from another thread.
    float evm_db() const { return _evm.load(std::memory_order_relaxed); }
    float rssi_db() const { return _rssi.load(std::memory_order_relaxed); }
    float cfo() const { return _cfo.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        bool progress = false;

        const size_t moved = std::min({_staging.size(), out->space(), _drain.size()});
        if (moved > 0) {
            _staging.readN(_drain.data(), moved);
            out->writeN(_drain.data(), moved);
            progress = true;
        }

        // Feed only once the previous chunk's payloads have all left: one
        // execute() can complete several frames, and the staging ring is the
        // only place they can go. Backing up here backpressures the framer
        // instead of dropping a packet.
        if (_staging.size() == 0) {
            const size_t n = std::min(in.size(), _samples.size());
            if (n > 0) {
                in.readN(_samples.data(), n);
                flexframesync_execute(_fs, _samples.data(), static_cast<unsigned int>(n));
                progress = true;
            }
        }
        return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                        : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSpaceOrSamples);
    }

private:
    static constexpr size_t CHUNK = 4096;
    // A CHUNK-sample execute() completes at most ~7 frames (the shortest
    // flexframe of any modulation is >600 samples), and the callback cannot
    // backpressure, so staging is sized well past that and is not tunable.
    static constexpr size_t STAGING_PACKETS = 64;

    static int on_frame(unsigned char* /*header*/, int header_valid,
                        unsigned char* payload, unsigned int payload_len, int payload_valid,
                        framesyncstats_s stats, void* userdata) {
        return static_cast<PacketDeframerBlock*>(userdata)
            ->accept(header_valid, payload, payload_len, payload_valid, stats);
    }

    int accept(int header_valid, unsigned char* payload, unsigned int payload_len,
               int payload_valid, framesyncstats_s stats) {
        if (!header_valid || !payload_valid || payload == nullptr || payload_len != _packet_bytes) {
            return 0;
        }
        if (_staging.space() < _packet_bytes) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        _staging.writeN(payload, _packet_bytes);
        _evm.store(stats.evm, std::memory_order_relaxed);
        _rssi.store(stats.rssi, std::memory_order_relaxed);
        _cfo.store(stats.cfo, std::memory_order_relaxed);
        return 0;
    }

    size_t _packet_bytes;
    cler::Channel<uint8_t> _staging;
    flexframesync _fs = nullptr;
    std::vector<std::complex<float>> _samples;
    std::vector<uint8_t> _drain;

    std::atomic<uint64_t> _dropped{0};
    std::atomic<float> _evm{0.0f};
    std::atomic<float> _rssi{0.0f};
    std::atomic<float> _cfo{0.0f};
};
