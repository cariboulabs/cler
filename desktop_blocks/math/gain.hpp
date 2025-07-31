#include "cler.hpp"

//a one to one gain block over arbitrary types
template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, const T gain_value, const size_t buffer_size = 1024)
        : cler::BlockBase(name), in(buffer_size), _gain(gain_value) { // Default 1024 for 4KB minimum
    }
    ~GainBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_process = std::min(read_size, write_size);
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                write_ptr[i] = read_ptr[i] * _gain;
            }
            in.commit_read(to_process);
            out->commit_write(to_process);
        }
        return cler::Empty{};
    }

    private:
        T _gain;
};