#include "cler.hpp"

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in;

    AddBlock(const char* name, size_t num_inputs, size_t in_buffer_size, size_t in_work_size):
     cler::BlockBase(name), _num_inputs(num_inputs), _work_size(in_work_size) {
        if (num_inputs < 2) {
            throw std::invalid_argument("AddBlock requires at least two input channels");
        }
        
        //Our ringbuffers are not copy/move so we cant use std::vector
        //As such, we use a raw array of cler::Channel<T>
        in = new cler::Channel<T>[num_inputs];
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<T>(in_buffer_size);
        }
        _tmp = new T[_work_size];

     }
    ~AddBlock() {
        delete[] _tmp;
        delete[] in;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
    // Ensure there is enough space in the output channel
    if (out->space() < _work_size) {
        return cler::Error::NotEnoughSpace;
    }

    for (size_t i = 0; i < _num_inputs; ++i) {
        if (in[i].size() < _work_size) {
            return cler::Error::NotEnoughSamples;
        }
    }

    for (size_t i = 0; i < _work_size; ++i) {
        T sum = T{};
        for (size_t j = 0; j < _num_inputs; ++j) {
            T val;
            in[j].pop(val);
            sum += val;
        }
        out->push(sum);
    }
    return cler::Empty{};
}

    private:
        size_t _num_inputs;
        size_t _work_size;
        T* _tmp;
};