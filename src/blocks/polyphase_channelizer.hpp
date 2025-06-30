#include "cler.hpp"
#include "liquid.h"
#include <vector>
#include <memory>
#include <complex>
#include <cassert>

struct PolyphaseChannelizer : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    PolyphaseChannelizer(const char* name, size_t num_channels, size_t buffer_size, size_t work_size)
        : cler::BlockBase(name), in(buffer_size), _work_size(work_size), _num_channels(num_channels) {

        _ch = firpfbch2_crcf_create_kaiser(LIQUID_ANALYZER, num_channels, 0, 80.0f);
        _tmp = new float[num_channels];\
        _y = new std::complex<float>[num_channels];
    }
    ~PolyphaseChannelizer() {
        delete[] _tmp;
        delete[] _y;
        if (_ch) firpfbch2_crcf_destroy(_ch);
    }

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_channels && "Number of output channels must match the number of polyphase channels");
        static_assert((std::is_same_v<OChannels, cler::Channel<std::complex<float>>> && ...), 
                      "All output channels must be of type cler::Channel<std::complex<float>>");

        return cler::Empty{};
    }

private:
    size_t _work_size;
    size_t _num_channels;
    float* _tmp;
    std::complex<float>* _y;
    firpfbch2_crcf _ch = nullptr;
};