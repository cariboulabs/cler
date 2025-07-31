#pragma once
#include "cler.hpp"
#include <chrono>
#include <iostream>

template <typename T>
struct ThroughputBlock : public cler::BlockBase {
    cler::Channel<T> in;

    ThroughputBlock(std::string name, size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(std::move(name)),
          in(buffer_size),
          _start_time(std::chrono::high_resolution_clock::now()),
          _buffer_size(buffer_size)
    {
        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        _tmp = new T[buffer_size];
    }

    ~ThroughputBlock() {
        delete[] _tmp;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Try zero-copy path first (for doubly mapped buffers)
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr && read_size > 0) {
            auto [write_ptr, write_size] = out->write_dbf();
            if (write_ptr && write_size > 0) {
                // ULTIMATE FAST PATH: Direct copy between doubly-mapped buffers
                size_t to_transfer = std::min(read_size, write_size);
                std::memcpy(write_ptr, read_ptr, to_transfer * sizeof(T));
                in.commit_read(to_transfer);
                out->commit_write(to_transfer);
                _samples_passed += to_transfer;
                return cler::Empty{};
            }
        }

        // Fall back to standard approach
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        in.readN(_tmp, transferable);
        out->writeN(_tmp, transferable);
        _samples_passed += transferable;

        return cler::Empty{};
    }

    void report() {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - _start_time;

        double seconds = elapsed.count();
        double throughput = (_samples_passed) / seconds;

        std::cout << "[ThroughputBlock] \"" << this->name() << "\" statistics:\n";
        std::cout << "  Total samples passed: " << _samples_passed << "\n";
        std::cout << "  Elapsed time (s):     " << seconds << "\n";
        std::cout << "  Throughput (samples/s): " << throughput << "\n";
    }

    size_t samples_passed() const {
        return _samples_passed;
    }

private:
    size_t _samples_passed = 0;
    std::chrono::high_resolution_clock::time_point _start_time;
    T* _tmp = nullptr;
    size_t _buffer_size;
};
