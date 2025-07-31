#include "cler.hpp"

//a one to one gain block over arbitrary types
template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, const T gain_value, const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _gain(gain_value) {
        _tmp = new T[buffer_size];
        _buffer_size = buffer_size;
    }
    ~GainBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Try zero-copy path first (for doubly mapped buffers)
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr && read_size > 0) {
            auto [write_ptr, write_size] = out->write_dbf();
            if (write_ptr && write_size > 0) {
                // ULTIMATE FAST PATH: Process directly between doubly-mapped buffers
                size_t to_process = std::min(read_size, write_size);
                for (size_t i = 0; i < to_process; ++i) {
                    write_ptr[i] = read_ptr[i] * _gain;
                }
                in.commit_read(to_process);
                out->commit_write(to_process);
                return cler::Empty{};
            }
        }

        // Fall back to standard approach
        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }
        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }
        size_t transferable = std::min({available_space, available_samples, _buffer_size});

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
        size_t _buffer_size;
};