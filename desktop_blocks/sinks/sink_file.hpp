#pragma once

#include "cler.hpp"
#include <fstream>
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

        _filename = filename;

        _file.open(_filename, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!_file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + std::string(filename));
        }
    }

    ~SinkFileBlock() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_file.is_open()) {
            return cler::Error::TERM_IOError;
        }

        T* ptr1, *ptr2;
        size_t sz1, sz2;
        size_t available_samples = in.peek_write(ptr1, sz1, ptr2, sz2);

        if (available_samples == 0) {
            _file.flush();
            return cler::Error::NotEnoughSamples;
        }

        if (sz1 > 0 && ptr1) {
            _file.write(reinterpret_cast<char*>(ptr1), sz1 * sizeof(T));
        }
        if (sz2 > 0 && ptr2) {
            _file.write(reinterpret_cast<char*>(ptr2), sz2 * sizeof(T));
        }

        if (!_file) {
            return cler::Error::TERM_IOError;
        }
        return cler::Empty{};
    }

private:
    const char* _filename;
    std::ofstream _file;
    size_t _buffer_size;
};
