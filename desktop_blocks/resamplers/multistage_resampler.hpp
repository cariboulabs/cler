#pragma once

#include "liquid.h"
#include "cler.hpp"
#include <type_traits>

template <typename>
inline constexpr bool dependent_false_v = false;

template <typename T>
struct MultiStageResamplerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    MultiStageResamplerBlock(const char* name, const float ratio, const float attenuation,
        const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _ratio(ratio)
    {
        if constexpr (std::is_same_v<T, float>) {
            _msresamp_r = msresamp_rrrf_create(ratio, attenuation);
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _msresamp_c = msresamp_crcf_create(ratio, attenuation);
        } else {
            static_assert(dependent_false_v<T>, "MultiStageResamplerBlock only supports float or std::complex<float>");
        }

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        if (ratio <= 0.0f) {
            throw std::invalid_argument("Ratio must be greater than zero.");
        }
        if (attenuation < 0.0f) {
            throw std::invalid_argument("Attenuation must be non-negative.");
        }


        _tmp_in = new T[buffer_size];
        _tmp_out = new T[buffer_size];
        _buffer_size = buffer_size;
    }

    ~MultiStageResamplerBlock() {
        delete[] _tmp_in;
        delete[] _tmp_out;

        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_destroy(_msresamp_r);
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            msresamp_crcf_destroy(_msresamp_c);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        size_t available_samples = in.size();
        size_t output_space = out->space();
        size_t input_limit_by_output = output_space / _ratio;

        size_t transferable = std::min({available_samples, input_limit_by_output, _buffer_size});

        if (transferable == 0) {
            return cler::Error::NotEnoughSamples;
        }

        in.readN(_tmp_in, transferable);

        unsigned int n_resampled = 0;

        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_execute(
                _msresamp_r,
                _tmp_in,
                transferable,
                _tmp_out,
                &n_resampled
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            msresamp_crcf_execute(
                _msresamp_c,
                reinterpret_cast<liquid_float_complex*>(_tmp_in),
                transferable,
                reinterpret_cast<liquid_float_complex*>(_tmp_out),
                &n_resampled
            );
        }

        out->writeN(_tmp_out, n_resampled);

        return cler::Empty{};
    }

private:
    T* _tmp_in;
    T* _tmp_out;
    float _ratio;
    size_t _buffer_size;

    msresamp_rrrf _msresamp_r = nullptr;
    msresamp_crcf _msresamp_c = nullptr;
};
