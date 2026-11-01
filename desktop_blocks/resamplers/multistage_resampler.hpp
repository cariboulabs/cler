#pragma once

#include "liquid.h"
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <type_traits>
#include <new>

template <typename T>
struct MultiStageResamplerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    MultiStageResamplerBlock(const char* name, const float ratio, const float attenuation,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _ratio(ratio), _attenuation(attenuation)
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (ratio <= 0.0f) {
            cler::panic("Ratio must be greater than zero.");
        }
        if (attenuation < 0.0f) {
            cler::panic("Attenuation must be non-negative.");
        }

        static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                      "MultiStageResamplerBlock only supports float or std::complex<float>");

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _input_buffer = new (std::nothrow) T[_buffer_size];
        if (!_input_buffer) {
            cler::panic("Failed to allocate input buffer");
        }
        create(ratio);
    }

    // Only while the graph is stopped: the input channel keeps its samples, the
    // filter state restarts from zero.
    void set_ratio(const float ratio) {
        if (ratio <= 0.0f) {
            cler::panic("Ratio must be greater than zero.");
        }
        destroy();
        create(ratio);
    }

    float ratio() const { return _ratio; }

    ~MultiStageResamplerBlock() {
        delete[] _input_buffer;
        destroy();
    }

    // liquid-dsp's msresamp needs contiguous in+out arrays and the output count is
    // data-dependent (not exactly input*ratio), so it can't write straight into the
    // channel's dbf window without risking an overrun; readN/writeN + temp buffers stays.
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        size_t available_input = in.size();
        size_t available_output = out->space();

        if (available_input == 0) {
            return cler::Error::NotEnoughSamples;
        }

        if (available_output == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Cap input by output space / ratio: downsampling (ratio<1) needs more input
        // than output space, upsampling the reverse.
        size_t max_input_by_output = static_cast<size_t>(available_output / _ratio);
        size_t max_input = std::min({available_input, max_input_by_output, _buffer_size});

        if (max_input == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_input_buffer, max_input);

        unsigned int n_resampled = 0;

        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_execute(
                _msresamp_r,
                _input_buffer,
                max_input,
                _output_buffer,
                &n_resampled
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            msresamp_crcf_execute(
                _msresamp_c,
                reinterpret_cast<liquid_float_complex*>(_input_buffer),
                max_input,
                reinterpret_cast<liquid_float_complex*>(_output_buffer),
                &n_resampled
            );
        }

        out->writeN(_output_buffer, n_resampled);

        return cler::Empty{};
    }

private:
    void create(const float ratio) {
        _ratio = ratio;
        if constexpr (std::is_same_v<T, float>) {
            _msresamp_r = msresamp_rrrf_create(ratio, _attenuation);
            if (!_msresamp_r) {
                cler::panic("Failed to create multi-stage resampler for float");
            }
        } else {
            _msresamp_c = msresamp_crcf_create(ratio, _attenuation);
            if (!_msresamp_c) {
                cler::panic("Failed to create multi-stage resampler for complex float");
            }
        }
        // msresamp can emit slightly more than buffer_size * ratio samples per call
        // (interpolator/decimator state carried across calls); +100 is a safety margin.
        _output_buffer_size = static_cast<size_t>(_buffer_size * _ratio + 100);
        _output_buffer = new (std::nothrow) T[_output_buffer_size];
        if (!_output_buffer) {
            cler::panic("Failed to allocate output buffer");
        }
    }

    void destroy() {
        delete[] _output_buffer;
        _output_buffer = nullptr;
        if constexpr (std::is_same_v<T, float>) {
            if (_msresamp_r) msresamp_rrrf_destroy(_msresamp_r);
            _msresamp_r = nullptr;
        } else {
            if (_msresamp_c) msresamp_crcf_destroy(_msresamp_c);
            _msresamp_c = nullptr;
        }
    }

    float _ratio;
    float _attenuation;
    size_t _buffer_size;
    size_t _output_buffer_size;

    T* _input_buffer = nullptr;
    T* _output_buffer = nullptr;

    msresamp_rrrf _msresamp_r = nullptr;
    msresamp_crcf _msresamp_c = nullptr;
};
