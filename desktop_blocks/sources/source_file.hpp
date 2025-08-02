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
    }

    ~SourceFileBlock() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        if (!_file.is_open()) {
            return cler::Error::TERM_IOError;
        }

        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Direct read into output buffer
        _file.read(reinterpret_cast<char*>(write_ptr), write_size * sizeof(T));
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
        
        out->commit_write(samples_read);
        return cler::Empty{};
    }

private:
    const char* _filename;
    bool _repeat;
    on_eof _callback;
    std::ifstream _file;
};
