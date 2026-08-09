#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <type_traits>

template <typename T, size_t NumInputs>
struct AddBlock : public cler::BlockBase {
    static_assert(NumInputs >= 2, "AddBlock requires at least two input channels");

    cler::Channel<T>* in = nullptr;

    AddBlock(const char* name, const size_t buffer_size = 0)
        : cler::BlockBase(name) {

        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        in = reinterpret_cast<cler::Channel<T>*>(_in_storage);
        for (size_t i = 0; i < NumInputs; ++i) {
            new (&in[i]) cler::Channel<T>(actual_buffer_size);
        }
    }
    ~AddBlock() {
        using TChannel = cler::Channel<T>;
        for (size_t i = 0; i < NumInputs; ++i) {
            in[i].~TChannel();
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (!write_ptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t min_available = write_size;
        for (size_t i = 0; i < NumInputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            min_available = std::min(min_available, read_size);
        }

        if (min_available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        std::fill_n(write_ptr, min_available, T{});

        for (size_t i = 0; i < NumInputs; ++i) {
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
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _in_storage[NumInputs];
};
