#pragma once

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include <complex>
#include <cstring>
#include <type_traits>

template <typename T>
struct SourceSigMFBlock : public cler::BlockBase {
    static_assert(std::is_same<T, float>::value || std::is_same<T, std::complex<float>>::value,
                  "SourceSigMFBlock supports float and std::complex<float>");
    static constexpr bool may_block = true;

    SourceSigMFBlock(const char* name, const char* path, bool repeat = false, size_t chunk_samples = 8192)
        : cler::BlockBase(name),
          _meta(sigmf::read_meta(path)),
          _repeat(repeat),
          _chunk_samples(chunk_samples == 0 ? 8192 : chunk_samples)
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
    }

    ~SourceSigMFBlock() {
        if (_fp) std::fclose(_fp);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_fp) {
            return cler::Error::TERM_EOFReached;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        const size_t sample_bytes = sigmf::datatype_size(_meta.datatype);
        size_t want = std::min(write_size, _chunk_samples);
        size_t got = std::fread(_raw.data(), 1, want * sample_bytes, _fp);
        size_t samples = got / sample_bytes;

        if (samples == 0) {
            if (_repeat) {
                std::clearerr(_fp);
                std::fseek(_fp, 0, SEEK_SET);
                return cler::Error::NotEnoughSamples;
            }
            std::fclose(_fp);
            _fp = nullptr;
            return cler::Error::NotEnoughSamples;
        }

        convert(_raw.data(), write_ptr, samples);
        out->commit_write(samples);
        return cler::Empty{};
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

    sigmf::Meta _meta;
    bool _repeat;
    size_t _chunk_samples;
    std::vector<uint8_t> _raw;
    FILE* _fp = nullptr;
};
