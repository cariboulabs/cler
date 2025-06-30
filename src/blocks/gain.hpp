#include "cler.hpp"

template <typename T>
struct GainBlock : public cler::BlockBase<GainBlock> {
    cler::Channel<T> in;

    GainBlock(const char* name, T gain_value, size_t buffer_size, size_t work_size):
     BlockBase(name), gain(gain_value), in(buffer_size), _work_size(work_size) {}

    cler::Result<cler::Empty, ClerError> procedure_impl(cler::Channel<T>* out) {
        if (in.size() < work_size) {
            return ClerError::NotEnoughSamples;
        }
        if (out->space() < work_size) {
            return ClerError::NotEnoughSpace;
        }

        size_t read = in.readN(_tmp, work_size);
        for (size_t i = 0; i < work_size; ++i) {
            _tmp[i] *= gain;
        }
        size_t written = out->writeN(_tmp, work_size);

        return cler::Empty{};
    }

    private:
        T _gain;
        float _tmp[work_size] = {0.0f};
        size_t _work_size
};