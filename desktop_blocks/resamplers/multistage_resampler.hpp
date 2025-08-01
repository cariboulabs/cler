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
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _ratio(ratio)
    {
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements of type T");
        }
        if (ratio <= 0.0f) {
            throw std::invalid_argument("Ratio must be greater than zero.");
        }
        if (attenuation < 0.0f) {
            throw std::invalid_argument("Attenuation must be non-negative.");
        }

        if constexpr (std::is_same_v<T, float>) {
            _msresamp_r = msresamp_rrrf_create(ratio, attenuation);
            if (!_msresamp_r) {
                throw std::runtime_error("Failed to create multi-stage resampler for float");
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _msresamp_c = msresamp_crcf_create(ratio, attenuation);
            if (!_msresamp_c) {
                throw std::runtime_error("Failed to create multi-stage resampler for complex float");
            }
        } else {
            static_assert(dependent_false_v<T>, "MultiStageResamplerBlock only supports float or std::complex<float>");
        }

        _buffer_size = buffer_size;
    }

    ~MultiStageResamplerBlock() {
        if constexpr (std::is_same_v<T, float>) {
            if (_msresamp_r) {
                msresamp_rrrf_destroy(_msresamp_r);
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            if (_msresamp_c) {
                msresamp_crcf_destroy(_msresamp_c);
            }
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        // Use doubly-mapped buffers for optimal performance (throws if not available)
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Calculate max input samples we can process given output space
        // For downsample (ratio < 1), we need more input samples than output space
        // For upsample (ratio > 1), we need fewer input samples than output space
        size_t max_input_by_write_space = static_cast<size_t>(write_size / _ratio);
        size_t samples_to_process = std::min(read_size, max_input_by_write_space);
        unsigned int n_resampled = 0;
        
        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_execute(
                _msresamp_r,
                const_cast<float*>(read_ptr),
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
