#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

//a many to one gain block over arbitrary types
template <typename T>
struct AddBlock : public cler::BlockBase {
    cler::Channel<T>* in = nullptr;

    AddBlock(const char* name, const size_t num_inputs, const size_t buffer_size = 0)
        : cler::BlockBase(name), _num_inputs(num_inputs) {

        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        if (num_inputs < 2) {
            cler::panic("AddBlock requires at least two input channels");
        }

        in = static_cast<cler::Channel<T>*>(
            ::operator new[](num_inputs * sizeof(cler::Channel<T>), std::nothrow));
        if (!in) {
            cler::panic("Failed to allocate memory for input channels");
        }

        for (size_t i = 0; i < num_inputs; ++i) {
            new (&in[i]) cler::Channel<T>(actual_buffer_size);
            register_input(in[i]);
        }
    }
    ~AddBlock() {
        cleanup_channels(_num_inputs);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (!write_ptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t min_available = write_size;
        for (size_t i = 0; i < _num_inputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            min_available = std::min(min_available, read_size);
        }

        if (min_available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        std::fill_n(write_ptr, min_available, T{});

        for (size_t i = 0; i < _num_inputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            for (size_t j = 0; j < min_available; ++j) {
                write_ptr[j] += read_ptr[j];
            }
            in[i].commit_read(min_available);
        }

        out->commit_write(min_available);
        return cler::Empty{};
    }

    private:
        void cleanup_channels(size_t count) {
            if (!in) return;
            using TChannel = cler::Channel<T>;
            for (size_t i = 0; i < count; ++i) {
                in[i].~TChannel();
            }
            ::operator delete[](in);
            in = nullptr;
        }

        size_t _num_inputs;
};