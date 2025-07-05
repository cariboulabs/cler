#pragma once
#include "cler.hpp"
#include <chrono>

template <typename T>
struct ThrottleBlock : public cler::BlockBase {
    static constexpr size_t BUFFER_SIZE = 1024;

    cler::Channel<T> in;

    ThrottleBlock(const char* name, size_t sps)
        : cler::BlockBase(name),
          in(BUFFER_SIZE),
          _sps(sps),
          _interval(1.0 / static_cast<double>(sps)),
          _next_tick(std::chrono::high_resolution_clock::now())
    {
        if (_sps == 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Pop one sample, push one sample
        // Note: If we do a batch size, we will encause jittering downstream
        //       working 1 sample at a time is slow, but we dont mind about slow if we are throttling
        T sample;
        in.pop(sample);
        out->push(sample);

        // Compute target time for the next sample
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
