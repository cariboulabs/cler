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
        const size_t buffer_size = 1024)
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


        _buffer_size = buffer_size;
    }

    ~MultiStageResamplerBlock() {
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

        // Use doubly-mapped buffers for optimal performance (throws if not available)
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t samples_to_process = std::min({read_size, input_limit_by_output, write_size / static_cast<size_t>(_ratio)});
        unsigned int n_resampled = 0;
        
        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_execute(
                _msresamp_r,
                read_ptr,
                samples_to_process,
                write_ptr,
                &n_resampled
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            msresamp_crcf_execute(
                _msresamp_c,
                reinterpret_cast<liquid_float_complex*>(const_cast<T*>(read_ptr)),
                samples_to_process,
                reinterpret_cast<liquid_float_complex*>(write_ptr),
                &n_resampled
            );
        }
        
        in.commit_read(samples_to_process);
        out->commit_write(n_resampled);

        return cler::Empty{};
    }

private:
    float _ratio;
    size_t _buffer_size;

    msresamp_rrrf _msresamp_r = nullptr;
    msresamp_crcf _msresamp_c = nullptr;
};
