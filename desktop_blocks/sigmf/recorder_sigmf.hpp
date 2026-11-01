#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"

#include <atomic>
#include <complex>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

// Record-on-demand SigMF sink: always drains its input, writes ci16_le +
// meta only between start() and stop() (GUI-thread calls). procedure() never
// allocates; the conversion buffer is preallocated and files are opened in
// start().
struct SigMFRecorderBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<std::complex<float>> in;

    SigMFRecorderBlock(const char* name, double sample_rate, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _rate(sample_rate)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        _conv.resize(2 * (1 << 16));
    }

    ~SigMFRecorderBlock() { stop(); }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        if (_recording.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_fp) {
                size_t done = 0;
                while (done < rsize) {
                    const size_t n = std::min(rsize - done, _conv.size() / 2);
                    for (size_t i = 0; i < n; ++i) {
                        const auto& s = rptr[done + i];
                        _conv[2 * i] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, s.real() * 32767.0f)));
                        _conv[2 * i + 1] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, s.imag() * 32767.0f)));
                    }
                    if (std::fwrite(_conv.data(), sizeof(int16_t), 2 * n, _fp) != 2 * n) {
                        std::fclose(_fp);
                        _fp = nullptr;
                        _recording.store(false, std::memory_order_release);
                        _failed.store(true, std::memory_order_release);
                        break;
                    }
                    done += n;
                    _samples.fetch_add(n, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    // GUI thread. Opens <prefix>_YYYYmmdd_HHMMSS.sigmf-{meta,data}; the meta
    // carries the given centre frequency.
    bool start(const std::string& prefix, double center_frequency_hz) {
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::gmtime(&now));
        return start_at(prefix + "_" + stamp, center_frequency_hz);
    }

    // exact base path, no stamp appended
    bool start_at(const std::string& base, double center_frequency_hz) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_fp) return false;
        _base = base;
        _fp = std::fopen((base + ".sigmf-data").c_str(), "wb");
        if (!_fp) return false;
        sigmf::Meta meta;
        meta.datatype = sigmf::Datatype::ci16_le;
        meta.sample_rate = _rate;
        sigmf::Capture cap;
        cap.sample_start = 0;
        cap.frequency = center_frequency_hz;
        cap.has_frequency = true;
        cap.datetime = sigmf::utc_now();
        meta.captures.push_back(cap);
        if (!sigmf::write_meta(_base + ".sigmf-meta", meta)) {
            std::fclose(_fp);
            _fp = nullptr;
            std::remove((_base + ".sigmf-data").c_str());
            return false;
        }
        _samples.store(0, std::memory_order_relaxed);
        _failed.store(false, std::memory_order_release);
        _recording.store(true, std::memory_order_release);
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        _recording.store(false, std::memory_order_release);
        if (_fp) {
            std::fclose(_fp);
            _fp = nullptr;
        }
    }

    bool recording() const { return _recording.load(std::memory_order_acquire); }
    // one-shot: true after a write failure stopped the recording, cleared by the next start
    bool take_failure() { return _failed.exchange(false, std::memory_order_acq_rel); }
    // graph stopped and not recording only; the rate lands in the next start()'s meta
    void set_rate(double rate) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_fp) return;
        _rate = rate;
    }
    uint64_t bytes() const { return _samples.load(std::memory_order_relaxed) * 2 * sizeof(int16_t); }
    uint64_t samples() const { return _samples.load(std::memory_order_relaxed); }
    double sample_rate() const { return _rate; }
    std::string base() const { std::lock_guard<std::mutex> lock(_mutex); return _base; }

private:
    double _rate;
    std::vector<int16_t> _conv;
    mutable std::mutex _mutex;
    FILE* _fp = nullptr;
    std::string _base;
    std::atomic<bool> _recording{false};
    std::atomic<bool> _failed{false};
    std::atomic<uint64_t> _samples{0};
};
