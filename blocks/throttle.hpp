#pragma once
#include "cler.hpp"
#include <chrono>
#include <thread>

template <typename T>
struct ThrottleBlock : public cler::BlockBase {
    cler::Channel<T> in; 

    ThrottleBlock(const char* name, int sps, size_t work_size)
        : cler::BlockBase(name),
          in(work_size),
          _sps(sps),
          _work_size(work_size)
    {
        if (_sps <= 0) {
            throw std::invalid_argument("Sample rate must be greater than zero.");
        }
        if (_work_size == 0) {
            throw std::invalid_argument("Work size must be greater than zero.");
        }
        _interval = std::chrono::duration<double>(_work_size / static_cast<double>(_sps));
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* out) {
        const T* in_values;
        size_t available_in;
        size_t n_readable = in.peek_read(in_values, _work_size, &available_in);
        if (available_in < _work_size) {
            return cler::Error::NotEnoughSamples;
        }

        T* out_values;
        size_t available_out;
        size_t n_writable = out->peek_write(out_values, _work_size, &available_out);
        if (available_out < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        // Copy samples through
        for (size_t i = 0; i < _work_size; ++i) {
            out_values[i] = in_values[i];
        }

        in.commit_read(_work_size);
        out->commit_write(_work_size);

        // Throttle to maintain real-time sample rate
        auto now = std::chrono::steady_clock::now();
        if (_last_time.time_since_epoch().count() != 0) {
            auto elapsed = now - _last_time;
            if (elapsed < _interval) {
                std::this_thread::sleep_for(_interval - elapsed);
            }
        }
        _last_time = std::chrono::steady_clock::now();

        return cler::Empty{};
    }

private:
    int _sps;
    size_t _work_size;
    std::chrono::duration<double> _interval;
    std::chrono::steady_clock::time_point _last_time{};
};