#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <cstdio>

// Shared helpers for the FEC and framing blocks.
//
// liquid compiles its convolutional and Reed-Solomon codecs only when it is
// built against libfec (`LIBFEC_ENABLED`); the copy fetched by this project is
// not, so LIQUID_FEC_CONV_* and LIQUID_FEC_RS_M8 exist in the enum but
// fec_create() returns NULL for them. Probe before creating so the failure is a
// named panic rather than a null dereference.
inline bool fec_scheme_available(fec_scheme scheme) {
    fec q = fec_create(scheme, nullptr);
    if (!q) return false;
    fec_destroy(q);
    return true;
}

inline fec fec_create_or_panic(fec_scheme scheme, const char* who) {
    fec q = fec_create(scheme, nullptr);
    if (!q) {
        std::fprintf(stderr, "%s: ", who);
        cler::panic("fec scheme unavailable in this liquid build "
                    "(convolutional and Reed-Solomon codecs need libfec)");
    }
    return q;
}
