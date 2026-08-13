#pragma once

#include "imgui.h"

namespace cler::palette {

constexpr ImVec4 rgba(unsigned r, unsigned g, unsigned b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// Neutral greys with a blue accent, shared with the flowgraph GUI's tokens: a
// saturated red on every frame and title bar is tiring to sit in front of.
inline constexpr ImVec4 bg0       = rgba(0x0e, 0x11, 0x16);
inline constexpr ImVec4 bg1       = rgba(0x16, 0x1b, 0x22);
inline constexpr ImVec4 bg2       = rgba(0x1e, 0x24, 0x2d);
inline constexpr ImVec4 border    = rgba(0x2a, 0x32, 0x3d);
inline constexpr ImVec4 border_hi = rgba(0x3a, 0x44, 0x4f);
inline constexpr ImVec4 fg        = rgba(0xe6, 0xed, 0xf3);
inline constexpr ImVec4 muted     = rgba(0x8b, 0x98, 0xa9);
inline constexpr ImVec4 faint     = rgba(0x6b, 0x77, 0x87);
inline constexpr ImVec4 accent    = rgba(0x2b, 0x5f, 0xa8);
inline constexpr ImVec4 accent_hi = rgba(0x39, 0x87, 0xe5);
inline constexpr ImVec4 accent_bg = rgba(0x16, 0x28, 0x3c);
inline constexpr ImVec4 ok        = rgba(0x2e, 0x8b, 0x57);
inline constexpr ImVec4 warn      = rgba(0xb8, 0x77, 0x0a);
inline constexpr ImVec4 danger    = rgba(0xc0, 0x20, 0x2e);

inline constexpr ImVec4 plot_series[] = {
    rgba(0x39, 0x87, 0xe5),
    rgba(0x19, 0x9e, 0x70),
    rgba(0xc9, 0x85, 0x00),
    rgba(0x00, 0xa3, 0xb4),
    rgba(0x90, 0x85, 0xe9),
    rgba(0xc9, 0xb4, 0x8a),
    rgba(0xd6, 0x6a, 0x8a),
    rgba(0xe6, 0x67, 0x67),
};

}
