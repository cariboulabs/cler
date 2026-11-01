#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// RBDS/RDS bit-level decoder: takes differentially-decoded data bits, finds
// block sync via the 10-bit syndromes, assembles groups, and keeps the
// station text (PI, PTY, PS, RadioText). No DSP here, no allocation.
namespace rds {

struct Station {
    uint16_t pi = 0;
    uint8_t pty = 0;
    bool tp = false;
    bool ta = false;
    bool synced = false;
    char ps[9] = {};       // programme service name (8 chars)
    char rt[65] = {};      // radiotext (up to 64 chars)
    uint32_t groups_ok = 0;
    uint32_t blocks_bad = 0;
    uint32_t blocks_corrected = 0;
    uint32_t blocks_total = 0;
};

class Decoder {
public:
    static constexpr uint32_t OFFSET_A = 0x0FC, OFFSET_B = 0x198, OFFSET_C = 0x168,
                              OFFSET_CP = 0x350, OFFSET_D = 0x1B4;

    // Syndrome of a 26-bit block: 16 data + 10 check bits, generator
    // g(x) = x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1.
    static uint32_t syndrome(uint32_t block) {
        uint32_t reg = 0;
        for (int i = 25; i >= 0; --i) {
            reg = (reg << 1) | ((block >> i) & 1u);
            if (reg & 0x400u) reg ^= 0x5B9u;
        }
        return reg & 0x3FF;
    }

    // Burst error correction: the syndrome of the received block XOR the
    // expected offset is the syndrome of the error pattern alone; a table maps
    // it back to the burst. The code can correct 5-bit bursts, but with
    // random (not bursty) bit errors every extra table entry is a chance to
    // turn an uncorrectable block into a valid-looking wrong one, so only
    // bursts up to MAX_BURST bits are corrected (single-bit errors dominate).
    // Returns the corrected block, or 0 when the syndrome is not a short burst.
    static constexpr int MAX_BURST = 2;
    static uint32_t correct(uint32_t block, uint32_t offset) {
        static const BurstTable table;
        const uint32_t s = (syndrome(block) ^ offset) & 0x3FF;
        if (s == 0) return block;
        const uint32_t e = table.at(s);
        return e ? (block ^ e) : 0;
    }

    static int offset_index(uint32_t s) {
        switch (s) {
            case OFFSET_A: return 0;
            case OFFSET_B: return 1;
            case OFFSET_C: return 2;
            case OFFSET_CP: return 2;
            case OFFSET_D: return 3;
            default: return -1;
        }
    }

    // Feed one data bit (already differentially decoded). Returns true when a
    // complete group was accepted.
    bool push_bit(bool bit) {
        _shift = ((_shift << 1) | (bit ? 1u : 0u)) & 0x3FFFFFFu;
        ++_bits;
        if (!_station.synced) {
            const int idx = offset_index(syndrome(_shift));
            if (idx == 0) {
                _station.synced = true;
                _bits = 0;
                _blocks[0] = _shift >> 10;
                _valid = 1;
                _expect = 1;
                _bad_run = 0;
                ++_station.blocks_total;
            }
            return false;
        }
        if (_bits < 26) return false;
        _bits = 0;
        ++_station.blocks_total;
        if (_expect == 0) _valid = 0;
        const uint32_t s = syndrome(_shift);
        const int idx = offset_index(s);
        if (idx != _expect) {
            // a block that matches another offset is a sync slip, not noise
            const uint32_t want = _expect == 0 ? OFFSET_A : _expect == 1 ? OFFSET_B : _expect == 2 ? OFFSET_C : OFFSET_D;
            uint32_t fixed = idx < 0 ? correct(_shift, want) : 0;
            if (!fixed && idx < 0 && _expect == 2) fixed = correct(_shift, OFFSET_CP);
            if (fixed) {
                ++_station.blocks_corrected;
                _shift = fixed;
                return accept(_shift, syndrome(_shift));
            }
            ++_station.blocks_bad;
            if (++_bad_run >= 4 * 4) {
                _station.synced = false;
                _expect = 0;
            } else {
                _expect = (_expect + 1) % 4;
            }
            return false;
        }
        return accept(_shift, s);
    }

    const Station& station() const { return _station; }

    void reset() { *this = Decoder{}; }

private:
    bool accept(uint32_t block, uint32_t s) {
        const int idx = offset_index(s);
        _bad_run = 0;
        _valid |= 1u << idx;
        _blocks[idx] = block >> 10;
        _cprime = (idx == 2 && s == OFFSET_CP);
        _expect = (idx + 1) % 4;
        // a group is only as good as its worst block: a stale B would send
        // PS/RT characters to the wrong segment
        if (idx == 3 && _valid == 0xF) {
            parse_group();
            ++_station.groups_ok;
            return true;
        }
        return false;
    }

    struct BurstTable {
        std::array<uint32_t, 1024> map{};
        BurstTable() {
            // shortest burst wins a collision; bursts start and end with a set bit
            for (int len = 1; len <= MAX_BURST; ++len) {
                for (uint32_t pat = 1u << (len - 1); pat < (1u << len); ++pat) {
                    if (!(pat & 1u)) continue;
                    for (int pos = 0; pos + len <= 26; ++pos) {
                        const uint32_t e = pat << pos;
                        const uint32_t s = syndrome(e);
                        if (map[s] == 0) map[s] = e;
                    }
                }
            }
        }
        uint32_t at(uint32_t s) const { return map[s & 0x3FF]; }
    };

    void parse_group() {
        const uint32_t a = _blocks[0], b = _blocks[1], c = _blocks[2], d = _blocks[3];
        _station.pi = static_cast<uint16_t>(a);
        const uint32_t type = (b >> 12) & 0xF;
        const bool version_b = (b >> 11) & 1;
        _station.tp = (b >> 10) & 1;
        _station.pty = (b >> 5) & 0x1F;
        if (type == 0) {
            _station.ta = (b >> 4) & 1;
            const uint32_t seg = b & 0x3;
            _station.ps[2 * seg] = printable(d >> 8);
            _station.ps[2 * seg + 1] = printable(d & 0xFF);
            _station.ps[8] = 0;
        } else if (type == 2) {
            const bool ab = (b >> 4) & 1;
            if (ab != _rt_ab) {
                _rt_ab = ab;
                std::memset(_station.rt, 0, sizeof(_station.rt));
            }
            const uint32_t seg = b & 0xF;
            if (!version_b) {
                put_rt(4 * seg, c >> 8); put_rt(4 * seg + 1, c & 0xFF);
                put_rt(4 * seg + 2, d >> 8); put_rt(4 * seg + 3, d & 0xFF);
            } else {
                put_rt(2 * seg, d >> 8); put_rt(2 * seg + 1, d & 0xFF);
            }
            _station.rt[64] = 0;
        }
        (void)_cprime;
    }

    void put_rt(uint32_t pos, uint32_t ch) {
        if (pos >= 64) return;
        _station.rt[pos] = (ch == 0x0D) ? 0 : printable(ch);
    }

    static char printable(uint32_t ch) {
        return (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }

    Station _station;
    uint32_t _shift = 0;
    uint32_t _bits = 0;
    int _expect = 0;
    int _bad_run = 0;
    unsigned _valid = 0;
    bool _cprime = false;
    bool _rt_ab = false;
    std::array<uint32_t, 4> _blocks{};
};

// Encoder for tests and loopback: 16-bit words -> 26-bit blocks with the
// given offset, emitted MSB first.
inline uint32_t encode_block(uint16_t data, uint32_t offset) {
    const uint32_t shifted = static_cast<uint32_t>(data) << 10;
    const uint32_t check = Decoder::syndrome(shifted);
    return shifted | (check ^ offset);
}

inline const char* pty_name(uint8_t pty) {
    static const char* names[32] = {
        "None", "News", "Affairs", "Info", "Sport", "Educate", "Drama", "Culture",
        "Science", "Varied", "Pop M", "Rock M", "Easy M", "Light M", "Classics", "Other M",
        "Weather", "Finance", "Children", "Social", "Religion", "Phone In", "Travel", "Leisure",
        "Jazz", "Country", "Nation M", "Oldies", "Folk M", "Document", "TEST", "Alarm"};
    return names[pty & 31];
}

}  // namespace rds
