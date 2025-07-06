#pragma once

//this file includes things that might be optional for the user to include
//examples: io, hardware specific, logging, etc.

#include "cler.hpp"
#include <iostream>

namespace cler {
inline std::ostream& operator<<(std::ostream& os, cler::Error error) {
    return os << to_str(error);
}
}