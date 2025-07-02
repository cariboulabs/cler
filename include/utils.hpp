#pragma once

#include "cler.hpp"
#include <iostream>

inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}