#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/FogSettings.hpp"
#include "Panda/Renderer/PassTarget.hpp"

#include <Miren/Miren.hpp>
#include <Rain/Rain.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

class SceneBlocks;


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
