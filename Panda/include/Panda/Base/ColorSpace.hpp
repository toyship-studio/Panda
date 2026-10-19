#pragma once

#include "Panda/Base/Base.hpp"

#include <cmath>

namespace Panda {

// Authored colors (color picker, COLOR fields, light colors) are specified in sRGB,
// GPU math runs in linear light. The curve is consistent with the implicit tonemap
// (pow 1/2.2 in tonemap.frag) — the exact piecewise sRGB curve is not needed here.
inline float srgbToLinear(float channel) {
    return std::pow(std::abs(channel), 2.2f);
}

// Alpha is a coverage coefficient, not a color: it stays linear.
inline Color srgbToLinear(const Color &color) {
    return Color(srgbToLinear(color.x), srgbToLinear(color.y), srgbToLinear(color.z), color.w);
}

// Inverse conversion — for values that arrive in linear form (for example,
// glTF factors) which the editor shows as an authored sRGB color.
inline float linearToSrgb(float channel) {
    return std::pow(std::abs(channel), 1.f / 2.2f);
}

inline Color linearToSrgb(const Color &color) {
    return Color(linearToSrgb(color.x), linearToSrgb(color.y), linearToSrgb(color.z), color.w);
}

} // namespace Panda
