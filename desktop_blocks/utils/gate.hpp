#pragma once

#include "cler.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

// A monitoring tap that can be switched off at runtime: forwards while open,
// discards while closed. A closed gate consumes everything, because an upstream
// fanout advances by its slowest output and would otherwise stall the live path
// whenever this branch is idle.
// An open gate normally backpressures like any block. Only once its input is
// backing up towards full does it discard the excess and count it, so a slow or
// stalled decoder degrades itself instead of freezing audio.
template <typename T>
struct GateBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GateBlock(const char* name, bool open = false, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _open(open)
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        if (!_open.load(std::memory_order_relaxed)) {
            in.commit_read(rsize);
            return cler::Empty{};
        }

        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(rsize, wsize);
        if (n > 0) {
            std::memcpy(wptr, rptr, n * sizeof(T));
            out->commit_write(n);
        }
        size_t consumed = n;
        const size_t capacity = in.size() + in.space();
        if (n < rsize && in.size() - n > capacity - capacity / 4) {
            consumed = rsize;
            _dropped.fetch_add(rsize - n, std::memory_order_relaxed);
        }
        in.commit_read(consumed);
        if (consumed == 0) return cler::Error::NotEnoughSpaceOrSamples;
        return cler::Empty{};
    }

    void set_open(bool open) { _open.store(open, std::memory_order_relaxed); }
    bool open() const { return _open.load(std::memory_order_relaxed); }
    uint64_t dropped() const { return _dropped.load(std::memory_order_relaxed); }
    void clear_dropped() { _dropped.store(0, std::memory_order_relaxed); }

private:
    std::atomic<bool> _open;
    std::atomic<uint64_t> _dropped{0};
};
