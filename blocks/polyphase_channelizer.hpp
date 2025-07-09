#include "cler.hpp"
#include "liquid.h"
#include <vector>
#include <memory>
#include <complex>
#include <cassert>

struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    PolyphaseChannelizerBlock(const char* name,
                        size_t num_channels,
                        float  kaiser_attenuation,
                        size_t kaiser_filter_semilength,
                        size_t in_buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(in_buffer_size), _num_channels(num_channels) {

        assert(kaiser_filter_semilength > 0 && kaiser_filter_semilength < 9 && 
                "Filter length must be between 1 and 9, larger values ==> narrower transition band. 4 is usually a good default");

        _pfch = firpfbch2_crcf_create_kaiser(LIQUID_ANALYZER,
                                            num_channels,
                                            kaiser_filter_semilength,
                                            kaiser_attenuation);
        _tmp_in = new std::complex<float>[num_channels];
        _tmp_out = new std::complex<float>[num_channels];
    }
    ~PolyphaseChannelizerBlock() {
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

        if (in.size() < _num_channels) {
            return cler::Error::NotEnoughSamples;
        }

        size_t n_frames_by_samples = in.size() / _num_channels;
        size_t n_frames_by_space = std::min({outs->space()...});
        size_t num_frames = std::min(n_frames_by_samples, n_frames_by_space);

        if (num_frames == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < num_frames; ++i) {
            in.readN(_tmp_in, _num_channels);
            firpfbch2_crcf_execute(
                _pfch, 
                /*liquid uses float complex, which has same layout in memory as std::complex<float>,
                reinterpret_cast is required as static_cast wont work*/
                reinterpret_cast<liquid_float_complex*>(_tmp_in),
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
    size_t _num_channels;
    std::complex<float>* _tmp_in;
    std::complex<float>* _tmp_out;
    firpfbch2_crcf _pfch = nullptr;
};