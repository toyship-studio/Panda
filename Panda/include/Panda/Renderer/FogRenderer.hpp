#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/PassTarget.hpp"

#include <Miren/Miren.hpp>
#include <Rain/Rain.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

class SceneBlocks;

// "graphics.fog" of a world: the air between the camera and what it sees.
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

// What the fog pass needs of a frame besides its color and depth
// (PostprocessingFrameInputs::setView).
struct FogFrame final {
    FogSettings settings;
    glm::mat4 viewProj = glm::mat4(1.f);
    // The scene blocks of the frame (the camera, the sun, the environment light): the pass reads
    // the light of the world from them. nullptr — no fog.
    const SceneBlocks *scene = nullptr;
};

// The fog of a world (context/lighting.md, "Fog"): an exponential height fog as one pass over
// the finished scene, in linear HDR, before the exposure, the bloom and the chain of the
// project. The amount of air along the view ray has a closed form — no ray march, no noise. Its
// color is not a constant: the fog scatters the light of the world, so it takes the sky of the
// world near the horizon in the direction of the view and the sun around the sun, and follows
// the environment map, the time of day and the exposure without a setting.
// A world without fog runs no pass: the feature costs nothing when off. Transparent surfaces
// are fogged as what is behind them (they write no depth).
class FogRenderer final {
public:
    FogRenderer() = default;
    ~FogRenderer();
    FogRenderer(const FogRenderer &) = delete;
    FogRenderer &operator=(const FogRenderer &) = delete;

    // The scene color with the fog over it (RGBA16F, linear); the input as is when the world has
    // no fog.
    Miren::TextureHandle apply(
        Miren::TextureHandle sceneColor, Miren::TextureHandle depth, uint32_t width, uint32_t height,
        const FogFrame &frame, FullscreenQuad &quad
    );

private:
    Miren::ProgramHandle m_program;
    PassTarget m_target;
};

} // namespace Panda
