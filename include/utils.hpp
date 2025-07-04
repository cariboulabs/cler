#pragma once

#include "cler.hpp"
#include <bit>
#include <iostream>

namespace cler {
inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}
}