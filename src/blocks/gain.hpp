#include "cler.hpp"

template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, T gain_value, size_t buffer_size, size_t work_size):
     cler::BlockBase(name), in(buffer_size), _gain(gain_value), _work_size(work_size) {
        _tmp = new T[_work_size];
     }
    ~GainBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
        if (in.size() < _work_size) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        size_t read = in.readN(_tmp, _work_size);
        for (size_t i = 0; i < _work_size; ++i) {
            _tmp[i] *= _gain;
        }
        size_t written = out->writeN(_tmp, _work_size);

        return cler::Empty{};
    }

    private:
        T _gain;
        size_t _work_size;
        T* _tmp;
};