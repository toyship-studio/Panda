#pragma once

#include <Rain/Rain.hpp>

#include <cstdint>

namespace Panda {

// The settings of the project that say what a frame may cost (the project scope, `.Panda/
// project.json`; the export gives them to the game). What a world looks like is not here: it is
// components of its entities — the sky, the sun and its shadows, the camera and its ambient
// occlusion. These are how finely the engine draws them, the same for every world of the game.

// "shadows": the maps the shadows are drawn from — the cascades of the sun and the tiles of the
// lamps — and the offsets against the stripes a surface casts on itself (in texels of a map:
// independent of the scale of the scene). Whether a light casts shadows and how dark and soft
// they are is the light's (DirectionalLightComponent, PointLightComponent).
struct ShadowSettings final : public Rain::Codable {
    uint32_t cascadeCount = 3; // the parts the shadow distance is cut into: 1..RENDERER_MAX_SHADOW_CASCADES
    uint32_t tileSize = 1024;  // the side of the map of a cascade (texels)
    float distance = 100.f;    // how far from the camera the sun casts shadows (m)
    uint32_t lampTileSize = 512; // the side of the map of a lamp: a spot has one, a point light six
    float slopeBias = 2.f;       // the rasterizer slope bias of the depth pass
    float normalOffset = 1.f;    // the point looked up moves along its normal (texels)
    float receiverBias = 0.5f;   // …and towards the light (texels)

    RAIN_FIELDS_BEGIN(ShadowSettings)
    RAIN_FIELD(cascadeCount)
    RAIN_FIELD(tileSize)
    RAIN_FIELD(distance)
    RAIN_FIELD(lampTileSize)
    RAIN_FIELD(slopeBias)
    RAIN_FIELD(normalOffset)
    RAIN_FIELD(receiverBias)
    RAIN_FIELDS_END
};

// "ambientOcclusion": what the screen-space ambient occlusion may cost (SsaoRenderer). A pixel
// reads the depth 2 × slices × steps times (and five more for its own point and normal); at full
// resolution there are four times the pixels. More slices — less grain for the blur to hide; more
// steps — an occluder between two samples is not missed; full resolution — the occlusion follows
// thin details and silhouettes. Whether a camera has it and how far and how dark it reaches is the
// camera's (CameraComponent).
struct AmbientOcclusionSettings final : public Rain::Codable {
    int slices = 3;              // directions the surroundings of a pixel are cut in (1..MAX_SLICES)
    int steps = 4;               // depth samples along a direction, each way (1..MAX_STEPS)
    bool fullResolution = false; // at the resolution of the frame instead of half of it

    static constexpr int MAX_SLICES = 8;
    static constexpr int MAX_STEPS = 12;

    RAIN_FIELDS_BEGIN(AmbientOcclusionSettings)
    RAIN_FIELD(slices)
    RAIN_FIELD(steps)
    RAIN_FIELD(fullResolution)
    RAIN_FIELDS_END
};

} // namespace Panda
