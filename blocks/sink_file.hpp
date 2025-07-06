#pragma once

#include "cler.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in { cler::DEFAULT_BUFFER_SIZE };

    SinkFileBlock(const char* name, const char* filename)
        : cler::BlockBase(name), _filename(filename) {
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

        size_t to_write = cler::floor2(std::min(available_samples, cler::DEFAULT_BUFFER_SIZE));

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
};
