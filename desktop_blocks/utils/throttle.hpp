#pragma once
#include "cler.hpp"
#include "cler_utils.hpp"
#include <chrono>

template <typename T>
struct ThrottleBlock : public cler::BlockBase {
    cler::Channel<T> in;

    ThrottleBlock(const char* name, const size_t sps, size_t const buffer_size = 1024)
        : cler::BlockBase(name),
          in(buffer_size),
          _sps(sps),
          _interval(1.0 / static_cast<double>(sps)),
          _next_tick(std::chrono::high_resolution_clock::now())
    {
        if (_sps == 0) {
            cler::panic("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // One sample at a time by design: batching would jitter the downstream pacing this
        // block exists to produce. Slower is fine here since throttling means we want slow.
        T sample;
        in.pop(sample);
        out->push(sample);

        _next_tick += std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
            std::chrono::duration<double>(_interval));

        auto now = std::chrono::high_resolution_clock::now();
        if (now < _next_tick) {
            auto sleep_duration = _next_tick - now;
            std::this_thread::sleep_for(sleep_duration);
        } else {
            // If we fall behind, catch up (do not sleep)
            _next_tick = now;
        }

        return cler::Empty{};
    }

private:
    size_t _sps;
    double _interval;  // interval between samples in seconds
    std::chrono::high_resolution_clock::time_point _next_tick;
};
