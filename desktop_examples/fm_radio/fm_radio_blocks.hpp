#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <atomic>
#include <cmath>
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

#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include <variant>

// RF rate -> 240 kHz MPX rate. The ratio is a template parameter, so the
// supported RF rates are a fixed menu.
struct ChannelResampler : public cler::BlockBase {
    using Sample = std::complex<float>;
    static constexpr double MPX_RATE = 240e3;

    static bool supported(double rf_rate) { return index_for(rf_rate) >= 0; }
    static const char* menu() { return "1.92, 2.0, 2.4, 3.0 or 4.8 MS/s"; }

    ChannelResampler(const char* name, double rf_rate, float attenuation_db, size_t buffer_size)
        : cler::BlockBase(name), _r(make(rf_rate, attenuation_db, buffer_size)),
          in(std::visit([](auto& r) -> cler::Channel<Sample>& { return r.in; }, _r)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Sample>* out) {
        return std::visit([&](auto& r) { return r.procedure(out); }, _r);
    }

private:
    using V = std::variant<RationalResamplerBlock<1, 8, 160>,    // 1.92 MS/s
                           RationalResamplerBlock<3, 25, 160>,   // 2.0
                           RationalResamplerBlock<1, 10, 160>,   // 2.4
                           RationalResamplerBlock<2, 25, 160>,   // 3.0
                           RationalResamplerBlock<1, 20, 160>>;  // 4.8

    static int index_for(double rf_rate) {
        const double rates[] = {1.92e6, 2.0e6, 2.4e6, 3.0e6, 4.8e6};
        for (int i = 0; i < 5; ++i) if (std::fabs(rf_rate - rates[i]) < 1.0) return i;
        return -1;
    }

    static V make(double rf_rate, float att, size_t n) {
        switch (index_for(rf_rate)) {
            case 0: return V(std::in_place_index<0>, "Channel", att, n);
            case 1: return V(std::in_place_index<1>, "Channel", att, n);
            case 2: return V(std::in_place_index<2>, "Channel", att, n);
            case 3: return V(std::in_place_index<3>, "Channel", att, n);
            case 4: return V(std::in_place_index<4>, "Channel", att, n);
        }
        cler::panic("fm_radio: unsupported RF sample rate");
    }

    V _r;

public:
    cler::Channel<Sample>& in;
};
