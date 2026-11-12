// PANDA_LIGHTS — the lights of the frame (shader feature `lights`): ambientColorIntensity (rgb —
// linear color, w — intensity), sunDirectionIntensity (xyz — TOWARD the sun, w — intensity, 0 —
// no sun), sunColor (linear), lightCounts.x — the number of point lights,
// pointLightPositionsRange[8] (xyz — position, w — radius), pointLightColorsIntensity[8] (rgb —
// linear color, w — intensity). Colors are linear: the engine converts the authored sRGB. The
// block is generated from Panda::LightsUbo; with the feature off the include is a compile error.
#include "panda/blocks/lights.hlsli"
