#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <algorithm>
#include <chrono>

template <typename T>
struct ThrottleBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    cler::Channel<T> in;

    ThrottleBlock(const char* name, const size_t sps, size_t const buffer_size = 1024)
        : cler::BlockBase(name),
          in(buffer_size),
          _sps(static_cast<double>(sps))
    {
        if (sps == 0) {
            cler::panic("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        if (!_pacing_started) {
            start_pacing();
        }

        const clock::time_point now = wait_until_sample_is_due(_emitted);

        const size_t due = samples_due_by(now);
        const size_t owed = due > _emitted ? due - _emitted : 1;
        const size_t transferable = std::min(owed, std::min(in.size(), out->space()));

        for (size_t i = 0; i < transferable; ++i) {
            T sample;
            in.pop(sample);
            out->push(sample);
        }
        _emitted += transferable;

        if (transferable < owed) {
            drop_pacing_debt(now);
        }

        return cler::Empty{};
    }

private:
    using clock = std::chrono::high_resolution_clock;

    void start_pacing() {
        _epoch = clock::now();
        _emitted = 0;
        _pacing_started = true;
    }

    clock::time_point wait_until_sample_is_due(size_t sample_index) const {
        const clock::time_point due_at = _epoch + time_to_emit(sample_index);
        const clock::time_point now = clock::now();
        if (now >= due_at) {
            return now;
        }
        std::this_thread::sleep_for(due_at - now);
        return clock::now();
    }

    void drop_pacing_debt(clock::time_point now) {
        _epoch = now - time_to_emit(_emitted);
    }

    clock::duration time_to_emit(size_t samples) const {
        return std::chrono::duration_cast<clock::duration>(
            std::chrono::duration<double>(static_cast<double>(samples) / _sps));
    }

    size_t samples_due_by(clock::time_point now) const {
        return static_cast<size_t>(std::chrono::duration<double>(now - _epoch).count() * _sps);
    }

    double _sps;
    bool _pacing_started = false;
    size_t _emitted = 0;
    clock::time_point _epoch;
};
