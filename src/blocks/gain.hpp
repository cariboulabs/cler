#include "cler.hpp"

template <typename T, size_t BufferSize, size_t WorkSamples>
struct GainBlock : public cler::BlockBase<GainBlock> {
    cler::Channel<T, BufferSize> in;
    T gain;

    GainBlock(const char* name, T gain_value) :
     BlockBase(name), gain(gain_value) {}

    cler::Result<cler::Empty, ClerError> procedure_impl(cler::Channel<T>* out) {
        if (in.size() < WorkSamples) {
            return ClerError::NotEnoughSamples;
        }
        if (out->space() < WorkSamples) {
            return ClerError::NotEnoughSpace;
        }

        size_t read = in.readN(_tmp, WorkSamples);
        for (size_t i = 0; i < WorkSamples; ++i) {
            _tmp[i] *= gain;
        }
        size_t written = out->writeN(_tmp, WorkSamples);

        return cler::Empty{};
    }

    private:
        float _tmp[WorkSamples] = {0.0f};
};