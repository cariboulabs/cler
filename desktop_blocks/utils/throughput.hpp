#pragma once
#include "cler.hpp"
#include <chrono>
#include <iostream>

template <typename T>
struct ThroughputBlock : public cler::BlockBase {
    cler::Channel<T> in;

    ThroughputBlock(std::string name, size_t buffer_size = 0)
        : cler::BlockBase(std::move(name)),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _start_time(std::chrono::high_resolution_clock::now())
    {
        // If user provided a non-zero buffer size, validate it's sufficient
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements of type T");
        }
    }

    ~ThroughputBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_transfer = std::min(read_size, write_size);
        if (to_transfer > 0) {
            std::memcpy(write_ptr, read_ptr, to_transfer * sizeof(T));
            in.commit_read(to_transfer);
            out->commit_write(to_transfer);
            _samples_passed += to_transfer;
        }
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
};
