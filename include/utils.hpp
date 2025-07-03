#pragma once

#include "cler.hpp"
#include <bit>
#include <iostream>

inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}


size_t floor2(size_t x) {
    if (x == 0) return 0;
    return size_t(1) << (std::bit_width(x) - 1);
}