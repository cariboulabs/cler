#include "BitSequence.hpp"

static size_t num_nibbles(uint64_t value) {
    size_t bits = 0;
    while (value) {
        ++bits;
        value >>= 4; // shift 1 nibble at a time
    }
    return bits;
}  

BitSequence::BitSequence(uint32_t val) {
    this->length = num_nibbles(val) * 4;
    this->bits = 0;

    for (size_t i = 0; i < this->length; ++i) {
        if ((val >> (this->length - 1 - i)) & 1) {
            this->bits |= (1 << i);  //also flip bits left to right
        }
    }
}

uint8_t BitSequence::get_bit(size_t idx) const {
    if (idx >= this->length) {
        return 2; // Return 2 to indicate an error
    }
    return (this->bits >> idx) & 1;
}

uint8_t BitSequence::get_byte(size_t idx) const {
    if (idx >= (this->length + 7) / 8) {
        return 0; // Return 0 to indicate an error
    }
    return (this->bits >> (idx * 8)) & 0xFF; // Get the byte at idx

}

std::string BitSequence::into_string() const {
    std::string result;
    for (size_t i = 0; i < this->length; ++i) {
        result += (get_bit(i) ? '1' : '0');
    }
    return result;
}

std::string BitSequence::into_compare_string(BitSequence other) const{
    //goes bit by bit, and colors green if same, red if different
    std::string result;
    for (size_t i = 0; i < this->length; ++i) {
        if (this->get_bit(i) == other.get_bit(i)) {
            result += "\033[1;32m"; // green color for matching bit
        } else {
            result += "\033[1;31m"; // red color for non-matching bit
        }
        result += std::to_string(get_bit(i));
        result += "\033[0m"; // reset color
    }
    return result;
}

bool BitSequence::set_bit(size_t idx, bool value) {
    if (idx >= this->length) {
        return false; // Return false to indicate an error
    }
    if (value) {
        this->bits |= (1 << idx); // Set the bit at idx to 1
    } else {
        this->bits &= ~(1 << idx); // Set the bit at idx to 0
    }
    return true; // Return true to indicate success
}