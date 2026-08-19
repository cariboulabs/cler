#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <atomic>
#include <complex>

// Small glue for the radio: volume on the interleaved audio, and a real ->
// complex adapter so the MPX baseband can go into PlotCSpectrumBlock.
struct VolumeBlock : public cler::BlockBase {
    cler::Channel<float> in;

    VolumeBlock(const char* name, float volume = 1.0f, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _volume(volume) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(rsize, wsize);
        if (n == 0) return cler::Error::NotEnoughSpaceOrSamples;
        const float g = _volume.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) wptr[i] = rptr[i] * g;
        in.commit_read(n);
        out->commit_write(n);
        return cler::Empty{};
    }

    void set_volume(float v) { _volume.store(v, std::memory_order_relaxed); }
    float volume() const { return _volume.load(std::memory_order_relaxed); }

private:
    std::atomic<float> _volume;
};

struct RealToComplexBlock : public cler::BlockBase {
    cler::Channel<float> in;

    RealToComplexBlock(const char* name, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(rsize, wsize);
        if (n == 0) return cler::Error::NotEnoughSpaceOrSamples;
        for (size_t i = 0; i < n; ++i) wptr[i] = {rptr[i], 0.0f};
        in.commit_read(n);
        out->commit_write(n);
        return cler::Empty{};
    }
};
