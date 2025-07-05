#include "cler.hpp"

//a one to one gain block over arbitrary types
template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, T gain_value)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE), _gain(gain_value) {
        _tmp = new T[cler::DEFAULT_BUFFER_SIZE];
    }
    ~GainBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }
        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }
        size_t transferable = cler::floor2(std::min({available_space, available_samples, cler::DEFAULT_BUFFER_SIZE}));

        size_t read = in.readN(_tmp, transferable);
        for (size_t i = 0; i < transferable; ++i) {
            _tmp[i] *= _gain;
        }
        size_t written = out->writeN(_tmp, transferable);

        return cler::Empty{};
    }

    private:
        T _gain;
        size_t _work_size;
        T* _tmp;
};