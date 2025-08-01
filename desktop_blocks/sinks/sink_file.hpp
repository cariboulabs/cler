#pragma once

#include "cler.hpp"
#include <cstdio>
#include <stdexcept>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkFileBlock(const char* name, const char* filename, size_t buffer_size = 1024)
        : cler::BlockBase(name), in(buffer_size), _filename(filename) { // Default 1024 for 4KB minimum

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
        if (_internal_buffer) {
            delete[] _internal_buffer;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        // Use zero-copy path
        auto [span_ptr, span_size] = in.read_dbf();
        
        if (span_size > 0) {
            // Single write, no copy
            size_t written = std::fwrite(span_ptr, sizeof(T), span_size, _fp);
            if (written != span_size) return cler::Error::TERM_IOError;
            in.commit_read(written);
        }
        return cler::Empty{};
    }

private:
    const char* _filename;
    FILE* _fp = nullptr;
    char* _internal_buffer = nullptr;
};
