#pragma once

#include "cler.hpp"
#include <fstream>
#include <stdexcept>

template <typename T>
struct SourceFileBlock : public cler::BlockBase {
    typedef void (*on_eof)(const char* filename);

    SourceFileBlock(const char* name, const char* filename, const bool repeat = true, on_eof callback = nullptr)
        : cler::BlockBase(name),
          _filename(filename),
          _repeat(repeat),
          _callback(callback)
    {
        _file.open(_filename, std::ios::binary);
        if (!_file.is_open()) {
            throw std::runtime_error("Failed to open file: " + std::string(filename));
        }

        _tmp = new T[cler::DEFAULT_BUFFER_SIZE];
    }

    ~SourceFileBlock() {
        if (_file.is_open()) {
            _file.close();
        }
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        if (!_file.is_open()) {
            return cler::Error::IOError;
        }

        size_t available_space = out->space();
        if (available_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_read = cler::floor2(std::min(available_space, cler::DEFAULT_BUFFER_SIZE));

        _file.read(reinterpret_cast<char*>(_tmp), to_read * sizeof(T));
        size_t samples_read = _file.gcount() / sizeof(T);

        if (samples_read == 0) {
            if (_file.eof() && _repeat) {
                _file.clear(); // Clear EOF flag
                _file.seekg(0, std::ios::beg);
                return cler::Empty{}; // Don't return an error, just let the flowgraph/loop call this again
            } else {
                if (_callback) {
                    _callback(_filename);
                }
                if (_file.is_open()) {_file.close();}
                return cler::Empty{};
            }
        }

        out->writeN(_tmp, samples_read);

        return cler::Empty{};
    }

private:
    const char* _filename;
    bool _repeat;
    on_eof _callback;
    std::ifstream _file;
    T* _tmp;
};
