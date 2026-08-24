#pragma once

#include "cler.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

// A monitoring tap that can be switched off at runtime: forwards while open,
// discards while closed. It always consumes its whole input, because an
// upstream fanout advances by its slowest output and would otherwise stall the
// live path whenever this branch is idle or behind.
// ponytail: an open gate drops whatever does not fit downstream and counts it;
// the decoder branches run in real time so the counter stays at zero. Give the
// output channel more room, or backpressure here, if a slow consumer matters.
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
        if (_open.load(std::memory_order_relaxed)) {
            auto [wptr, wsize] = out->write_dbf();
            const size_t n = std::min(rsize, wsize);
            std::memcpy(wptr, rptr, n * sizeof(T));
            out->commit_write(n);
            _dropped.fetch_add(rsize - n, std::memory_order_relaxed);
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    void set_open(bool open) { _open.store(open, std::memory_order_relaxed); }
    bool open() const { return _open.load(std::memory_order_relaxed); }
    uint64_t dropped() const { return _dropped.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> _open;
    std::atomic<uint64_t> _dropped{0};
};
