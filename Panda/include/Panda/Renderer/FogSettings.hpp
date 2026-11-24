#pragma once

#include "Panda/Base/Base.hpp"

#include <Rain/Rain.hpp>

namespace Panda {

// The fog of a world: the air between the camera and what it sees — a part of the sky of the
// world (SkyComponent::fog), drawn by FogRenderer.
struct FogSettings final : public Rain::Codable {
    bool enabled = false;
    // How much of the light a meter of air at the base height takes away. 0.01 hides half of a
    // thing 70 m away; a hazy day is 0.002, a thick fog 0.1.
    float density = 0.01f;
    // The fog lies low: every `heightFalloff` meters above baseHeight halve its density
    // (valleys and the ground are in it, hilltops and the upper sky are clear). 0 — the same
    // density at every height.
    float heightFalloff = 20.f;
    float baseHeight = 0.f;    // the height the density is given at (m, the world Y)
    float startDistance = 0.f; // the air closer than this to the camera is clear (m)
    float maxOpacity = 1.f;    // the most the fog hides: below 1 the far things stay visible through it
    // The color of the fog is the light of the world scattered by it: the sky of the world near
    // the horizon in the direction of the view, plus the sun where the view goes towards it.
    // tint multiplies the sky part; sunScatter scales the glow around the sun (0 — none).
    Color tint = Color(1.f, 1.f, 1.f, 1.f);
    float sunScatter = 0.25f;
    bool affectSky = true; // the sky is behind all the air of its direction: the horizon dissolves, the zenith stays

    RAIN_FIELDS_BEGIN(FogSettings)
    RAIN_FIELD(enabled)
    RAIN_FIELD(density)
    RAIN_FIELD(heightFalloff)
    RAIN_FIELD(baseHeight)
    RAIN_FIELD(startDistance)
    RAIN_FIELD(maxOpacity)
    RAIN_FIELD(tint)
    RAIN_FIELD(sunScatter)
    RAIN_FIELD(affectSky)
    RAIN_FIELDS_END
};

} // namespace Panda
