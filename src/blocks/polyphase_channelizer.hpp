#include "cler.hpp"
#include "liquid/liquid.h"
#include <vector>
#include <memory>
#include <complex>
#include <cassert>

struct PolyphaseChannelizer : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    PolyphaseChannelizer(const char* name,
                        size_t num_channels,
                        float  kaiser_attenuation,
                        size_t kaiser_filter_length,
                        size_t in_buffer_size,
                        size_t in_work_size)
        : cler::BlockBase(name), in(in_buffer_size), _work_size(in_work_size), _num_channels(num_channels) {

        assert(kaiser_filter_length > 0 && kaiser_filter_length < 5 && "Filter length must be positive");
        // Remember!
        // work_in_samples = num_frames * num_channels
        // each output channel receives num_frames samples
        // each frame in input consists of num_channels samples
        if (in_work_size % num_channels != 0) {
            throw std::invalid_argument("Input work size must be a multiple of the number of channels");
        }
        _num_frames = in_work_size / num_channels;

        _pfch = firpfbch2_crcf_create_kaiser(LIQUID_ANALYZER, num_channels,
                                                kaiser_filter_length,
                                                kaiser_attenuation);
        _tmp_in = new std::complex<float>[in_work_size];
        _tmp_out = new std::complex<float>[num_channels];
    }
    ~PolyphaseChannelizer() {
        delete[] _tmp_in;
        delete[] _tmp_out;
        if (_pfch) firpfbch2_crcf_destroy(_pfch);
    }

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_channels && "Number of output channels must match the number of polyphase channels");
        static_assert((std::is_same_v<OChannels, cler::Channel<std::complex<float>>> && ...), 
                      "All output channels must be of type cler::Channel<std::complex<float>>");

        if (in.size() < _work_size) {
            return cler::Error::NotEnoughSamples;
        }

        //valdiate outs have enough space (cpp 17 fold expression) as we cant forloop on variadic templates
        if (( (outs->size() < _num_frames) || ... )) {
            return cler::Error::NotEnoughSpace;
        }

        in.readN(_tmp_in, _work_size);
        for (size_t i = 0; i < _num_frames; ++i) {
             firpfbch2_crcf_execute(
                _pfch, 
                /*liquid uses float complex, which has same layout in memory as std::complex<float>,
                reinterpret_cast is required as static_cast wont work*/
                reinterpret_cast<liquid_float_complex*>(&_tmp_in[i * _num_channels]),
                reinterpret_cast<liquid_float_complex*>(_tmp_out)
                );

            //cant for loop on varaiadic templates, so declaring a function and then calling it
            size_t idx = 0;
            auto push_outputs = [&](auto*... chs) {
                ((chs->push(_tmp_out[idx++])), ...);
            };
            push_outputs(outs...);
        }

        return cler::Empty{};
    }

private:
    size_t _work_size;
    size_t _num_channels;
    size_t _num_frames;
    std::complex<float>* _tmp_in;
    std::complex<float>* _tmp_out;
    firpfbch2_crcf _pfch = nullptr;
};