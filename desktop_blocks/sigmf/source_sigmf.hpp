#pragma once

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include <atomic>
#include <chrono>
#include <complex>
#include <cstring>
#include <thread>
#include <type_traits>

template <typename T>
struct SourceSigMFBlock : public cler::BlockBase {
    static_assert(std::is_same<T, float>::value || std::is_same<T, std::complex<float>>::value,
                  "SourceSigMFBlock supports float and std::complex<float>");
    static constexpr bool may_block = true;

    // transport = real-time pacing at the file rate plus seek/pause/loop/ended,
    // for interactive playback; EOF then parks (file stays open) instead of
    // terminating
    SourceSigMFBlock(const char* name, const char* path, bool repeat = false, size_t chunk_samples = 8192,
                     bool transport = false)
        : cler::BlockBase(name),
          _meta(sigmf::read_meta(path)),
          _repeat(repeat),
          _chunk_samples(chunk_samples == 0 ? 8192 : chunk_samples),
          _transport(transport),
          _loop(repeat)
    {
        constexpr bool complex_sink = std::is_same<T, std::complex<float>>::value;
        if (sigmf::datatype_is_complex(_meta.datatype) != complex_sink) {
            std::string msg = "SigMF datatype " + std::string(sigmf::datatype_name(_meta.datatype)) +
                              " does not match the block sample type";
            cler::panic(msg.c_str());
        }

        std::string file = sigmf::data_path(path);
        _fp = std::fopen(file.c_str(), "rb");
        if (!_fp) {
            std::string msg = "Failed to open SigMF data file: " + file;
            cler::panic(msg.c_str());
        }
        _raw.resize(_chunk_samples * sigmf::datatype_size(_meta.datatype));
        std::fseek(_fp, 0, SEEK_END);
        _total = static_cast<uint64_t>(std::ftell(_fp)) / sigmf::datatype_size(_meta.datatype);
        std::fseek(_fp, 0, SEEK_SET);
    }

    ~SourceSigMFBlock() {
        if (_fp) std::fclose(_fp);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_fp) {
            return cler::Error::TERM_EOFReached;
        }
        const size_t sample_bytes = sigmf::datatype_size(_meta.datatype);
        if (_transport) {
            const int64_t want_seek = _pending_seek.exchange(-1, std::memory_order_acq_rel);
            if (want_seek >= 0) {
                const uint64_t sample = std::min<uint64_t>(static_cast<uint64_t>(want_seek), _total);
                std::clearerr(_fp);
                std::fseek(_fp, static_cast<long>(sample * sample_bytes), SEEK_SET);
                _pos.store(sample, std::memory_order_relaxed);
                _ended.store(false, std::memory_order_relaxed);
                _started = false;
            }
            if (_ended.load(std::memory_order_relaxed) && _loop.load(std::memory_order_relaxed)) {
                std::clearerr(_fp);
                std::fseek(_fp, 0, SEEK_SET);
                _pos.store(0, std::memory_order_relaxed);
                _ended.store(false, std::memory_order_relaxed);
                _started = false;
            }
            if (_pause.load(std::memory_order_relaxed) || _ended.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                _started = false;
                return cler::Error::NotEnoughSamples;
            }
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t want = std::min(write_size, _chunk_samples);
        if (_transport) {
            const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
            if (!_started) { _epoch = clock::now() - to_duration(_emitted, rate); _started = true; }
            size_t due = samples_due(rate);
            if (due <= _emitted) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                due = samples_due(rate);
                if (due <= _emitted) return cler::Error::NotEnoughSamples;
            }
            want = std::min(want, due - _emitted);
            if (due - _emitted > rate / 10.0) _epoch = clock::now() - to_duration(_emitted, rate);
        }
        size_t got = std::fread(_raw.data(), 1, want * sample_bytes, _fp);
        size_t samples = got / sample_bytes;

        if (samples == 0) {
            if (_repeat || (_transport && _loop.load(std::memory_order_relaxed))) {
                std::clearerr(_fp);
                std::fseek(_fp, 0, SEEK_SET);
                _pos.store(0, std::memory_order_relaxed);
                return cler::Error::NotEnoughSamples;
            }
            if (_transport) {
                _ended.store(true, std::memory_order_relaxed);
                return cler::Error::NotEnoughSamples;
            }
            std::fclose(_fp);
            _fp = nullptr;
            return cler::Error::NotEnoughSamples;
        }

        convert(_raw.data(), write_ptr, samples);
        out->commit_write(samples);
        _emitted += samples;
        _pos.fetch_add(samples, std::memory_order_relaxed);
        return cler::Empty{};
    }

    void seek(double seconds) {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        _pending_seek.store(static_cast<int64_t>(std::max(0.0, seconds) * rate), std::memory_order_release);
    }
    void pause(bool p) { _pause.store(p, std::memory_order_relaxed); }
    bool paused() const { return _pause.load(std::memory_order_relaxed); }
    void set_loop(bool l) { _loop.store(l, std::memory_order_relaxed); }
    bool looping() const { return _loop.load(std::memory_order_relaxed); }
    bool ended() const { return _ended.load(std::memory_order_relaxed); }
    double pos_seconds() const {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        return static_cast<double>(_pos.load(std::memory_order_relaxed)) / rate;
    }
    double duration_seconds() const {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        return static_cast<double>(_total) / rate;
    }

    double sample_rate() const { return _meta.sample_rate; }
    double center_frequency() const { return _meta.center_frequency(); }
    sigmf::Datatype datatype() const { return _meta.datatype; }
    const sigmf::Meta& meta() const { return _meta; }

private:
    // one stored sample -> one T; complex types write two floats per sample
    void convert(const uint8_t* raw, T* out, size_t samples) const {
        float* dst = reinterpret_cast<float*>(out);
        const size_t components = std::is_same<T, std::complex<float>>::value ? 2 * samples : samples;
        switch (_meta.datatype) {
            case sigmf::Datatype::cf32_le:
            case sigmf::Datatype::rf32_le:
                std::memcpy(dst, raw, components * sizeof(float));
                break;
            case sigmf::Datatype::ci16_le:
            case sigmf::Datatype::ri16_le: {
                int16_t value;
                for (size_t i = 0; i < components; ++i) {
                    std::memcpy(&value, raw + i * sizeof(int16_t), sizeof(int16_t));
                    dst[i] = static_cast<float>(value) / 32768.0f;
                }
                break;
            }
            case sigmf::Datatype::ci8:
                for (size_t i = 0; i < components; ++i) {
                    dst[i] = static_cast<float>(static_cast<int8_t>(raw[i])) / 128.0f;
                }
                break;
            case sigmf::Datatype::cu8:
                for (size_t i = 0; i < components; ++i) {
                    dst[i] = (static_cast<float>(raw[i]) - 127.5f) / 127.5f;
                }
                break;
        }
    }

    using clock = std::chrono::steady_clock;
    size_t samples_due(double rate) const {
        return static_cast<size_t>(std::chrono::duration<double>(clock::now() - _epoch).count() * rate);
    }
    static clock::duration to_duration(size_t samples, double rate) {
        return std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(samples / rate));
    }

    sigmf::Meta _meta;
    bool _repeat;
    size_t _chunk_samples;
    bool _transport;
    std::vector<uint8_t> _raw;
    FILE* _fp = nullptr;
    uint64_t _total = 0;
    std::atomic<bool> _pause{false}, _loop{false}, _ended{false};
    std::atomic<int64_t> _pending_seek{-1};
    std::atomic<uint64_t> _pos{0};
    clock::time_point _epoch;
    size_t _emitted = 0;
    bool _started = false;
};
