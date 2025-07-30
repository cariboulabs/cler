#pragma once

#include "cler.hpp"
#include <cstdio>
#include <stdexcept>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkFileBlock(const char* name, const char* filename, size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _filename(filename) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        if (!filename || filename[0] == '\0') {
            throw std::invalid_argument("Filename must not be empty.");
        }

        _fp = std::fopen(_filename, "wb");
        if (!_fp) {
            throw std::runtime_error("Failed to open file for writing: " + std::string(filename));
        }

        // Optional: tune buffer to a larger size (e.g., 64 KB)
        _internal_buffer = new char[buffer_size * sizeof(T)];
        if (std::setvbuf(_fp, _internal_buffer, _IOFBF, buffer_size * sizeof(T)) != 0) {
            throw std::runtime_error("Failed to setvbuf() on file stream.");
        }
    }

    ~SinkFileBlock() {
        if (_fp) {
            std::fflush(_fp);
            std::fclose(_fp);
        }
        delete[] _internal_buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        const T* ptr1 = nullptr;
        const T* ptr2 = nullptr;
        size_t sz1 = 0, sz2 = 0;
        size_t available_samples = in.peek_read(ptr1, sz1, ptr2, sz2);

        if (available_samples == 0) {
            std::fflush(_fp);  // Only do this occasionally if needed
            return cler::Error::NotEnoughSamples;
        }

        if (sz1 > 0 && ptr1) {
            size_t written = std::fwrite(ptr1, sizeof(T), sz1, _fp);
            if (written != sz1) return cler::Error::TERM_IOError;
            in.commit_read(sz1);
        }

        if (sz2 > 0 && ptr2) {
            size_t written = std::fwrite(ptr2, sizeof(T), sz2, _fp);
            if (written != sz2) return cler::Error::TERM_IOError;
            in.commit_read(sz2);
        }

        return cler::Empty{};
    }

private:
    const char* _filename;
    FILE* _fp = nullptr;
    char* _internal_buffer = nullptr;
};
