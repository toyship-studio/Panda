// PANDA_LIGHTS — the lights of the frame (shader feature `lights`): ambientColorIntensity (rgb —
// linear color, w — intensity), sunDirectionIntensity (xyz — TOWARD the sun, w — intensity, 0 —
// no sun), sunColor (linear), and the lamps — point lights and spots: lightCounts.x of them in
// pointLightPositionsRange[8] (xyz — position, w — radius), pointLightColorsIntensity[8] (rgb —
// linear color, w — intensity), pointLightDirections[8] (xyz — where a spot shines),
// pointLightCones[8] (x — the cosine of the half-angle of the cone, -2 without one; y — the
// reciprocal of the width of the fade). Colors are linear: the engine converts the authored sRGB.
// The block is generated from Panda::LightsUbo; with the feature off the include is a compile
// error — a lit shader takes panda/lighting.hlsli.
//
// A shader does not read the lamp arrays itself: localLightCount() and localLight() are the lamps
// of the frame, with their falloff and their cones. The lamps are the shader feature `lamps`:
// without it there are none — the count is the constant 0 and a loop over them compiles to nothing.
#include "panda/blocks/lights.hlsli"

#if PANDA_FEATURE_LAMPS
int localLightCount() {
    return int(lightCounts.x);
}

// The light of a lamp that reaches a point: its color times its intensity, weakened by the
// distance and by the cone of a spot; `direction` — from the point toward the lamp. The intensity
// is the light at 1 m; it falls with the square of the distance (not closer than 10 cm: a lamp
// has a size) and a window brings it smoothly to zero at the radius. Black beyond the radius and
// outside the cone.
float3 localLight(int index, float3 worldPos, out float3 direction) {
    const float3 toLight = pointLightPositionsRange[index].xyz - worldPos;
    const float radius = pointLightPositionsRange[index].w;
    const float distanceSquared = dot(toLight, toLight);
    direction = toLight * rsqrt(max(distanceSquared, 1.0e-8));
    if (radius <= 0.0 || distanceSquared >= radius * radius) { return float3(0.0, 0.0, 0.0); }
    const float ratio = distanceSquared / (radius * radius);
    const float window = saturate(1.0 - ratio * ratio);
    const float falloff = window * window / max(distanceSquared, 0.01);
    const float2 cone = pointLightCones[index].xy;
    const float spot = saturate((dot(-direction, pointLightDirections[index].xyz) - cone.x) * cone.y);
    return pointLightColorsIntensity[index].rgb * (pointLightColorsIntensity[index].w * falloff * spot * spot);
}
#else
int localLightCount() {
    return 0;
}

float3 localLight(int index, float3 worldPos, out float3 direction) {
    direction = float3(0.0, 1.0, 0.0);
    return float3(0.0, 0.0, 0.0);
}
#endif
