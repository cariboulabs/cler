#pragma once

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include <complex>
#include <cstring>
#include <cmath>
#include <mutex>
#include <type_traits>

template <typename T>
struct SinkSigMFBlock : public cler::BlockBase {
    static_assert(std::is_same<T, float>::value || std::is_same<T, std::complex<float>>::value,
                  "SinkSigMFBlock supports float and std::complex<float>");
    static constexpr bool may_block = true;

    cler::Channel<T> in;

    SinkSigMFBlock(const char* name,
                   const char* path,
                   double sample_rate,
                   double center_frequency,
                   sigmf::Datatype datatype = sigmf::Datatype::cf32_le,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _base(sigmf::base_path(path))
    {
        constexpr bool complex_source = std::is_same<T, std::complex<float>>::value;
        if (sigmf::datatype_is_complex(datatype) != complex_source) {
            std::string msg = "SigMF datatype " + std::string(sigmf::datatype_name(datatype)) +
                              " does not match the block sample type";
            cler::panic(msg.c_str());
        }

        _meta.datatype = datatype;
        _meta.sample_rate = sample_rate;
        sigmf::Capture capture;
        capture.frequency = center_frequency;
        capture.has_frequency = true;
        capture.datetime = sigmf::utc_now();
        _meta.captures.push_back(capture);
        if (!sigmf::write_meta(_base, _meta)) {
            std::string msg = "Failed to write SigMF metadata: " + sigmf::meta_path(_base);
            cler::panic(msg.c_str());
        }

        std::string file = sigmf::data_path(_base);
        _fp = std::fopen(file.c_str(), "wb");
        if (!_fp) {
            std::string msg = "Failed to open SigMF data file for writing: " + file;
            cler::panic(msg.c_str());
        }
        _raw.resize(_chunk_samples * sigmf::datatype_size(datatype));
    }

    ~SinkSigMFBlock() {
        if (_fp) {
            std::fflush(_fp);
            std::fclose(_fp);
            _fp = nullptr;
        }
        std::lock_guard<std::mutex> lock(_annotations_mutex);
        sigmf::write_meta(_base, _meta);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        const size_t sample_bytes = sigmf::datatype_size(_meta.datatype);
        size_t samples = std::min(read_size, _chunk_samples);
        convert(read_ptr, _raw.data(), samples);
        if (std::fwrite(_raw.data(), sample_bytes, samples, _fp) != samples) {
            return cler::Error::TERM_IOError;
        }
        in.commit_read(samples);
        _samples_written += samples;
        return cler::Empty{};
    }

    void add_annotation(uint64_t sample_start, uint64_t sample_count, const char* label) {
        std::lock_guard<std::mutex> lock(_annotations_mutex);
        _meta.annotations.push_back(sigmf::make_annotation(sample_start, sample_count, label ? label : ""));
    }

    size_t samples_written() const { return _samples_written; }
    const sigmf::Meta& meta() const { return _meta; }

private:
    void convert(const T* samples_in, uint8_t* raw, size_t samples) const {
        const float* src = reinterpret_cast<const float*>(samples_in);
        const size_t components = std::is_same<T, std::complex<float>>::value ? 2 * samples : samples;
        switch (_meta.datatype) {
            case sigmf::Datatype::cf32_le:
            case sigmf::Datatype::rf32_le:
                std::memcpy(raw, src, components * sizeof(float));
                break;
            case sigmf::Datatype::ci16_le:
            case sigmf::Datatype::ri16_le:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 32768.0f;
                    if (scaled > 32767.0f) scaled = 32767.0f;
                    if (scaled < -32768.0f) scaled = -32768.0f;
                    int16_t value = static_cast<int16_t>(std::lrintf(scaled));
                    std::memcpy(raw + i * sizeof(int16_t), &value, sizeof(int16_t));
                }
                break;
            case sigmf::Datatype::ci8:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 128.0f;
                    if (scaled > 127.0f) scaled = 127.0f;
                    if (scaled < -128.0f) scaled = -128.0f;
                    raw[i] = static_cast<uint8_t>(static_cast<int8_t>(std::lrintf(scaled)));
                }
                break;
            case sigmf::Datatype::cu8:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 127.5f + 127.5f;
                    if (scaled > 255.0f) scaled = 255.0f;
                    if (scaled < 0.0f) scaled = 0.0f;
                    raw[i] = static_cast<uint8_t>(std::lrintf(scaled));
                }
                break;
        }
    }

    static constexpr size_t _chunk_samples = 8192;
    std::string _base;
    sigmf::Meta _meta;
    std::vector<uint8_t> _raw;
    FILE* _fp = nullptr;
    size_t _samples_written = 0;
    std::mutex _annotations_mutex;
};
