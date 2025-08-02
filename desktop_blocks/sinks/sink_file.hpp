#pragma once

#include "cler.hpp"
#include <cstdio>
#include <stdexcept>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkFileBlock(const char* name, const char* filename, size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _filename(filename) {

        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements of type T");
        }
        if (!filename || filename[0] == '\0') {
            throw std::invalid_argument("Filename must not be empty.");
        }

        _fp = std::fopen(_filename, "wb");
        if (!_fp) {
            throw std::runtime_error("Failed to open file for writing: " + std::string(filename));
        }

        // Calculate actual buffer size used
        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;


        if (std::setvbuf(_fp, nullptr, _IOFBF, actual_buffer_size * sizeof(T)) != 0) {
            std::fclose(_fp);
            _fp = nullptr;
            throw std::runtime_error("Failed to setvbuf() on file stream.");
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
};
