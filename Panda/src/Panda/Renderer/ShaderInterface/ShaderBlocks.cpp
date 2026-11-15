#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include "Panda/Renderer/EnvironmentLighting.hpp"
#include "Panda/Base/ColorSpace.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

FrameUbo FrameUbo::from(float time) {
    FrameUbo ubo{};
    ubo.frameTime = glm::vec4(time, 0.f, 0.f, 0.f);
    return ubo;
}

ViewUbo ViewUbo::from(const glm::vec3 &cameraPosition) {
    ViewUbo ubo{};
    ubo.cameraPosition = glm::vec4(cameraPosition, 0.f);
    return ubo;
}

LightsUbo LightsUbo::from(const WorldLightingData &lighting) {
    LightsUbo ubo{};
    const Color ambientLinear = srgbToLinear(lighting.ambientColor);
    const Color sunLinear = srgbToLinear(lighting.directionalLight.color);
    ubo.ambientColorIntensity = glm::vec4(ambientLinear.x, ambientLinear.y, ambientLinear.z, lighting.ambientIntensity);
    ubo.sunDirectionIntensity = glm::vec4(lighting.directionalLight.direction, lighting.directionalLight.intensity);
    ubo.sunColor = glm::vec4(sunLinear.x, sunLinear.y, sunLinear.z, sunLinear.w);
    ubo.lightCounts = glm::uvec4(lighting.pointLightCount, 0u, 0u, 0u);
    for (uint32_t i = 0; i < RENDERER_MAX_POINT_LIGHTS; ++i) {
        if (i >= lighting.pointLightCount) {
            ubo.pointLightPositionsRange[i] = glm::vec4(0.f);
            ubo.pointLightColorsIntensity[i] = glm::vec4(0.f);
            continue;
        }
        const PointLightData &light = lighting.pointLights[i];
        const Color colorLinear = srgbToLinear(light.color);
        ubo.pointLightPositionsRange[i] = glm::vec4(light.position, light.radius);
        ubo.pointLightColorsIntensity[i] = glm::vec4(colorLinear.x, colorLinear.y, colorLinear.z, light.intensity);
    }
    return ubo;
}

ShadowsUbo ShadowsUbo::from(const ShadowData &shadows) {
    // The matrices "world → (u, v, depth) of the tile", the slice boundaries, the atlas/bias
    // parameters. cascadeCount 0 — there are no shadows (shadowParams.w = 0).
    ShadowsUbo ubo{};
    const bool active = shadows.cascadeCount > 0 && shadows.atlas.get() != nullptr && shadows.strength > 0.f;
    for (uint32_t i = 0; i < RENDERER_MAX_SHADOW_CASCADES; ++i) { ubo.shadowMatrices[i] = shadows.textureMatrices[i]; }
    ubo.shadowSplits = glm::vec4(shadows.splits[0], shadows.splits[1], shadows.splits[2], shadows.splits[3]);
    const float atlasWidth = static_cast<float>(std::max(1u, shadows.cascadeCount * shadows.tileSize));
    const float atlasHeight = static_cast<float>(std::max(1u, shadows.tileSize));
    ubo.shadowParams = glm::vec4(1.f / atlasWidth, 1.f / atlasHeight, shadows.distance, active ? shadows.strength : 0.f);
    ubo.shadowTexelWorld =
        glm::vec4(shadows.texelWorld[0], shadows.texelWorld[1], shadows.texelWorld[2], shadows.texelWorld[3]);
    ubo.shadowDepthScale =
        glm::vec4(shadows.depthScale[0], shadows.depthScale[1], shadows.depthScale[2], shadows.depthScale[3]);
    ubo.shadowBias = glm::vec4(shadows.normalOffset, shadows.receiverBias, static_cast<float>(shadows.cascadeCount), 0.f);
    // A caster further than this widens the edge no more: the search for casters around a
    // point is as wide as the widest edge, and its cost and its noise grow with it.
    constexpr float WIDEST_PENUMBRA = 0.3f; // m
    const float softness = std::tan(glm::radians(std::clamp(shadows.softness, 0.f, 10.f)));
    ubo.shadowSoftness = glm::vec4(softness, WIDEST_PENUMBRA, 0.f, 0.f);
    return ubo;
}

OcclusionUbo OcclusionUbo::from(const OcclusionVolumeData &occlusion) {
    // The chunk grid, the window, the strengths, the volume texel and the slot table. Without a
    // volume — disabled (occlusionWindow.w = 0: the shader does not reach the sample).
    OcclusionUbo ubo{};
    const bool active = occlusion.volume.get() != nullptr && occlusion.windowChunks > 0 &&
                        (occlusion.aoStrength > 0.f || occlusion.sunStrengthFar > 0.f || occlusion.sunStrengthNear > 0.f);
    ubo.occlusionGrid = glm::vec4(
        occlusion.cellSize, static_cast<float>(occlusion.chunkCells), static_cast<float>(occlusion.tileSize),
        static_cast<float>(occlusion.slices)
    );
    ubo.occlusionWindow = glm::vec4(
        static_cast<float>(occlusion.windowMin.x), static_cast<float>(occlusion.windowMin.y),
        static_cast<float>(occlusion.windowChunks), active ? 1.f : 0.f
    );
    ubo.occlusionParams =
        glm::vec4(occlusion.aoStrength, occlusion.sunStrengthNear, occlusion.sunStrengthFar, occlusion.contrast);
    const float volumeSide = static_cast<float>(std::max(1u, occlusion.windowChunks * occlusion.tileSize));
    ubo.occlusionTexel = glm::vec4(
        1.f / volumeSide, 1.f / static_cast<float>(std::max(1u, occlusion.slices)), 1.f / volumeSide,
        occlusion.surfaceOffset
    );
    for (uint32_t i = 0; i < OCCLUSION_MAX_CHUNKS; ++i) {
        const OcclusionChunk &chunk = occlusion.chunks[i];
        ubo.occlusionChunks[i] =
            glm::vec4(static_cast<float>(chunk.chunk.x), static_cast<float>(chunk.chunk.y), chunk.originY, chunk.weight);
    }
    ubo.occlusionFocus = glm::vec4(occlusion.focus.x, occlusion.focus.y, occlusion.fadeRadius, occlusion.transmittanceBlend);
    return ubo;
}

EnvironmentUbo EnvironmentUbo::from(const EnvironmentData &environment) {
    // x — exposure, y — the highest mip (blurring of reflections by roughness), z — 1 if a map is
    // set (otherwise the slot holds the built-in sky gradient, PBR takes no reflections).
    EnvironmentUbo ubo{};
    TextureAsset *map = environment.map.get();
    float maxMip = 0.f;
    if (map) {
        const Size size = map->getSize();
        maxMip = std::floor(std::log2(std::max(1.f, std::max(size.x, size.y))));
    }
    // The light of the map exists once its atlas is baked: until then (and without a map) the
    // tiles are zero and the shaders read "no map".
    const bool lit = map != nullptr && environment.lighting.get() != nullptr;
    ubo.environmentParams = glm::vec4(std::max(environment.intensity, 0.f), maxMip, lit ? 1.f : 0.f, 0.f);
    if (lit) {
        const EnvironmentLighting::Tiles &tiles = EnvironmentLighting::tiles();
        for (size_t index = 0; index < tiles.size(); ++index) { ubo.environmentTiles[index] = tiles[index]; }
    }
    return ubo;
}

SsaoUbo SsaoUbo::from(const SsaoData &ssao) {
    SsaoUbo ubo{};
    SsaoData data = ssao; // isValid() of a Miren handle is non-const
    ubo.ssaoViewProj = ssao.viewProj;
    ubo.ssaoParams = glm::vec4(
        data.map.isValid() ? std::max(ssao.intensity, 0.f) : 0.f,
        Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 1.f : 0.f, 0.f, 0.f
    );
    return ubo;
}

WindUbo WindUbo::from(const glm::vec4 &phase) {
    WindUbo ubo{};
    ubo.sceneWind = phase;
    return ubo;
}

} // namespace Panda
