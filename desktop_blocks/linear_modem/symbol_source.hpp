#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// `count` symbols of a maximal-length sequence, bps bits each. The caller keeps
// the vector and hands the same one to SymbolSourceBlock and BERCounterBlock, so
// the reference sequence is shared data rather than a re-derived generator.
inline std::vector<uint8_t> prbs_symbols(unsigned int bps, size_t count) {
    msequence ms = msequence_create_default(10);
    std::vector<uint8_t> syms(count);
    for (size_t i = 0; i < count; ++i) {
        syms[i] = static_cast<uint8_t>(msequence_generate_symbol(ms, bps));
    }
    msequence_destroy(ms);
    return syms;
}

// Cycles a fixed symbol vector forever.
struct SymbolSourceBlock : public cler::BlockBase {
    SymbolSourceBlock(const char* name, std::vector<uint8_t> symbols)
        : cler::BlockBase(name), _symbols(std::move(symbols)) {
        if (_symbols.empty()) {
            cler::panic("SymbolSourceBlock requires a non-empty symbol vector");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t n = std::min(out->space(), _scratch.size());
        if (n == 0) {
            return cler::Error::NotEnoughSpace;
        }
        for (size_t i = 0; i < n; ++i) {
            _scratch[i] = _symbols[_pos];
            if (++_pos == _symbols.size()) _pos = 0;
        }
        out->writeN(_scratch.data(), n);
        return cler::Empty{};
    }

private:
    std::vector<uint8_t> _symbols;
    std::vector<uint8_t> _scratch = std::vector<uint8_t>(4096);
    size_t _pos = 0;
};
