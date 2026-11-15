#pragma once

#include <cstdint>
#include <string>

namespace Panda {

// The curve that brings the linear HDR frame to the display (the last pass of the frame,
// tonemap.frag). The numbers go to the shader (PANDA_POST, tonemapParams.x); the names are what a
// world file says ("tonemap" of a camera).
enum class Tonemap : uint32_t {
    AgX = 0,      // filmic: bright saturated light fades to white without a shift of hue. The default.
    Aces = 1,     // the film look of ACES (the fit of the RRT and ODT by S. Hill): more contrast, deeper colors
    Neutral = 2,  // Khronos PBR Neutral: colors of materials reach the display as authored, only highlights are compressed
    Reinhard = 3, // x / (1 + x): the old default of the engine
    None = 4,     // no curve: the linear frame is clamped at 1
};

const char *tonemapName(Tonemap curve); // "agx", "aces", "neutral", "reinhard", "none"
// An unknown name is the default curve.
Tonemap tonemapFromName(const std::string &name);

// How a camera exposes and develops its frame.
struct TonemapSettings final {
    Tonemap curve = Tonemap::AgX;
    float exposure = 0.f; // compensation in stops: the frame is multiplied by 2^exposure before the curve
    // The edges of the finished frame are smoothed (FXAA: one pass after the curve).
    bool antialiasing = true;
};

} // namespace Panda
