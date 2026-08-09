#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cstdio>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<T> in;

    SinkFileBlock(const char* name, const char* filename, size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size) {

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (!filename || filename[0] == '\0') {
            cler::panic("Filename must not be empty");
        }

        _fp = std::fopen(filename, "wb");
        if (!_fp) {
            cler::panic("Failed to open file for writing");
        }

        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        if (std::setvbuf(_fp, nullptr, _IOFBF, actual_buffer_size * sizeof(T)) != 0) {
            std::fclose(_fp);
            _fp = nullptr;
            cler::panic("Failed to setvbuf() on file stream");
        }
    }

    ~SinkFileBlock() {
        if (_fp) {
            std::fflush(_fp);
            std::fclose(_fp);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        auto [span_ptr, span_size] = in.read_dbf();
        if (span_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t written = std::fwrite(span_ptr, sizeof(T), span_size, _fp);
        if (written != span_size) return cler::Error::TERM_IOError;
        in.commit_read(written);
        return cler::Empty{};
    }

private:
    FILE* _fp = nullptr;
};
