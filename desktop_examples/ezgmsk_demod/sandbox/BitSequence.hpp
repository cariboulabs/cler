#pragma once
#include <cstdint>
#include <string>

class BitSequence {
public:
    uint32_t bits;    // Bits stored LSB-first
    size_t length;    // Number of meaningful bits, here incase leading zeros are present

    BitSequence(uint32_t val);
    BitSequence(size_t length, uint32_t bits) : bits(bits), length(length) {}
    uint8_t get_bit(size_t idx) const;
    uint8_t get_byte(size_t idx) const;
    bool set_bit(size_t idx, bool value);
    std::string into_string() const;
    std::string into_compare_string(BitSequence other) const;
};