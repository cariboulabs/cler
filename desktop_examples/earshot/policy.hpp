#pragma once

#include "desktop_blocks/demod/analog_demod.hpp"

#include <cstdio>
#include <string>
#include <vector>

// What the receiver will allow, given what the source actually carries. Pure, so
// the rules can be tested without a radio or a flowgraph.
namespace earshot {

inline std::string hz_str(double hz) {
    char b[32];
    if (hz >= 1e6) std::snprintf(b, sizeof(b), "%.4g MHz", hz / 1e6);
    else std::snprintf(b, sizeof(b), "%.4g kHz", hz / 1e3);
    return b;
}

inline double passband_for(AnalogDemodBlock::Mode m) {
    switch (m) {
        case AnalogDemodBlock::Mode::WBFM: return 200e3;
        case AnalogDemodBlock::Mode::NBFM: return 12.5e3;
        case AnalogDemodBlock::Mode::AM: return 10e3;
        default: return 3.2e3;
    }
}

// A mode whose passband is wider than the source cannot mean anything: the chain
// upsamples a narrow capture into the 240 kHz channel, it does not invent the
// bandwidth that was never captured. "" when the mode is usable.
inline std::string mode_disabled(AnalogDemodBlock::Mode m, double rate_hz) {
    const double pb = passband_for(m);
    if (pb <= rate_hz) return {};
    return "needs " + hz_str(pb) + "; this source is " + hz_str(rate_hz) + " wide";
}

// "none" and duplicates fold away; an unknown name fails the whole list, so a
// typo never silently runs a shorter set than the client asked for.
inline bool normalise_decoders(const std::vector<std::string>& names,
                               const std::vector<std::string>& known,
                               std::vector<std::string>& out) {
    out.clear();
    for (const auto& n : names) {
        if (n.empty() || n == "none") continue;
        bool ok = false;
        for (const auto& k : known) ok = ok || n == k;
        if (!ok) return false;
        bool dup = false;
        for (const auto& e : out) dup = dup || e == n;
        if (!dup) out.push_back(n);
    }
    return true;
}

}  // namespace earshot
