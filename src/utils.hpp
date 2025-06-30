#pragma once

#include "cler.hpp"
#include <iostream>

inline std::ostream& operator<<(std::ostream& os, ClerError error) {
    return os << to_str(error);
}