#pragma once

#include "cler.hpp"
#include <fstream>
#include <stdexcept>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkFileBlock(std::string name, const char* filename, size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(std::move(name)), in(buffer_size), _filename(filename) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        if (!filename || filename[0] == '\0') {
            throw std::invalid_argument("Filename must not be empty.");
        }

        _filename = filename;
        _buffer_size = buffer_size;

        _file.open(_filename, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!_file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + std::string(filename));
        }
        _tmp = new T[cler::DEFAULT_BUFFER_SIZE];
    }

    ~SinkFileBlock() {
        if (_file.is_open()) {
            _file.close();
        }
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_file.is_open()) {
            return cler::Error::IOError;
        }

        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t to_write = std::min(available_samples, _buffer_size);

        in.readN(_tmp, to_write);
        _file.write(reinterpret_cast<char*>(_tmp), to_write * sizeof(T));

        if (!_file) {
            return cler::Error::IOError;
        }

        return cler::Empty{};
    }

private:
    const char* _filename;
    std::ofstream _file;
    T* _tmp;
    size_t _buffer_size = cler::DEFAULT_BUFFER_SIZE;
};
