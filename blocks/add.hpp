#include "cler.hpp"

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in;

    AddBlock(const char* name, const size_t num_inputs, const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), _num_inputs(num_inputs) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }

        if (num_inputs < 2) {
            throw std::invalid_argument("AddBlock requires at least two input channels");
        }

        _buffer_size = buffer_size;
        
        // Our ringbuffers are not copy/move so we cant use std::vector
        // As such, we use a raw array of cler::Channel<T>
        // Allocate raw storage only, no default construction
        in = static_cast<cler::Channel<T>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<T>))
        );
        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<T>(buffer_size);
        }
     }
    ~AddBlock() {
        using TChannel = cler::Channel<T>; //cant template on Destructor...
        for (size_t i = 0; i < _num_inputs; ++i) {
            in[i].~TChannel();
        }
        ::operator delete[](in);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
    // Ensure there is enough space in the output channel
    size_t available_space = out->space();
    if (available_space == 0) {
        return cler::Error::NotEnoughSpace;
    }

    size_t min_available_samples = in[0].size();
    for (size_t i = 1; i < _num_inputs; ++i) {
        if (in[i].size() < min_available_samples) {
            min_available_samples = in[i].size();
        }
    }
    if (min_available_samples == 0) {
        return cler::Error::NotEnoughSamples;
    }

    size_t transferable = cler::floor2(std::min({available_space, min_available_samples, _buffer_size}));

    for (size_t i = 0; i < transferable; ++i) {
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
        size_t _buffer_size;
};