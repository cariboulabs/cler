#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// 8-bit interleaved IQ capture (hackrf_transfer -r, rtl_sdr) replayed in a loop.
struct SourceIQFileBlock : public cler::BlockBase {
    SourceIQFileBlock(const char* name, const std::string& path, size_t chunk = 1 << 16)
        : cler::BlockBase(name), _raw(2 * chunk) {
        _f = std::fopen(path.c_str(), "rb");
        if (!_f) cler::panic("SourceIQFileBlock: cannot open file");
    }
    ~SourceIQFileBlock() { if (_f) std::fclose(_f); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, wsize] = out->write_dbf();
        size_t n = std::min(wsize, _raw.size() / 2);
        if (n == 0) return cler::Error::NotEnoughSpace;
        size_t got = std::fread(_raw.data(), 2, n, _f);
        if (got == 0) { std::rewind(_f); got = std::fread(_raw.data(), 2, n, _f); }
        if (got == 0) return cler::Error::TERM_IOError;
        for (size_t i = 0; i < got; ++i) wptr[i] = {_raw[2 * i] / 128.0f, _raw[2 * i + 1] / 128.0f};
        out->commit_write(got);
        return cler::Empty{};
    }

private:
    FILE* _f = nullptr;
    std::vector<int8_t> _raw;
};
