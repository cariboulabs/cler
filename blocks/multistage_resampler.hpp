#pragma once

#include "liquid.h"
#include "cler.hpp"
#include <complex>
#include <algorithm>

struct MultiStageResampler : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    MultiStageResampler(const char* name, float ratio, float attenuation)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE), _ratio(ratio)
    {
        _tmp_in = new std::complex<float>[cler::DEFAULT_BUFFER_SIZE];
        _tmp_out = new std::complex<float>[cler::DEFAULT_BUFFER_SIZE];
        msresamp = msresamp_crcf_create(ratio, attenuation);
    }
    ~MultiStageResampler() {
        delete[] _tmp_in;
        delete[] _tmp_out;
        msresamp_crcf_destroy(msresamp);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out)
    {
        size_t available_samples = in.size();
        size_t space_limit = out->space() / _ratio;

        size_t transferable = cler::floor2(std::min({space_limit, available_samples, cler::DEFAULT_BUFFER_SIZE}));

        if (transferable == 0) {
            return cler::Error::NotEnoughSamples;
        }

        in.readN(_tmp_in, transferable);

        unsigned int n_decimated_samples = 0;
        msresamp_crcf_execute(
            msresamp,
            reinterpret_cast<liquid_float_complex*>(_tmp_in),
            transferable,
            reinterpret_cast<liquid_float_complex*>(_tmp_out),
            &n_decimated_samples
        );
        


        return cler::Empty{};
    }

private:
    std::complex<float>* _tmp_in;
    std::complex<float>* _tmp_out;
    float _ratio;
    msresamp_crcf msresamp;
};
