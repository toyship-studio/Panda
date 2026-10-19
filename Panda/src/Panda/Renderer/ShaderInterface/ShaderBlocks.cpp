#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"
#include "Panda/Base/ColorSpace.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

SceneLightsUbo SceneLightsUbo::from(const WorldLightingData &lighting) {
    SceneLightsUbo ubo{};
    const Color ambientLinear = srgbToLinear(lighting.ambientColor);
    const Color directionalLinear = srgbToLinear(lighting.directionalLight.color);
    ubo.ambientColorIntensity =
        glm::vec4(ambientLinear.x, ambientLinear.y, ambientLinear.z, lighting.ambientIntensity);
    ubo.directionalDirectionIntensity =
        glm::vec4(lighting.directionalLight.direction, lighting.directionalLight.intensity);
    ubo.directionalColor =
        glm::vec4(directionalLinear.x, directionalLinear.y, directionalLinear.z, directionalLinear.w);
    ubo.sceneLightCounts = glm::uvec4(lighting.pointLightCount, 0u, 0u, 0u);
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
    ubo.cameraPosition = glm::vec4(lighting.cameraPosition, 0.f);
    // Cascaded shadows: the matrices "world → (u, v, depth) of the tile", the slice boundaries,
    // the atlas/bias parameters. cascadeCount 0 — there are no shadows (shadowParams.w = 0).
    const ShadowData &shadows = lighting.shadows;
    const bool shadowsActive = shadows.cascadeCount > 0 && shadows.atlas.get() != nullptr && shadows.strength > 0.f;
    for (uint32_t i = 0; i < RENDERER_MAX_SHADOW_CASCADES; ++i) {
        ubo.shadowMatrices[i] = shadows.textureMatrices[i];
    }
    ubo.shadowSplits = glm::vec4(shadows.splits[0], shadows.splits[1], shadows.splits[2], shadows.splits[3]);
    const float atlasWidth = static_cast<float>(std::max(1u, shadows.cascadeCount * shadows.tileSize));
    const float atlasHeight = static_cast<float>(std::max(1u, shadows.tileSize));
    ubo.shadowParams =
        glm::vec4(1.f / atlasWidth, 1.f / atlasHeight, shadows.distance, shadowsActive ? shadows.strength : 0.f);
    ubo.shadowTexelWorld =
        glm::vec4(shadows.texelWorld[0], shadows.texelWorld[1], shadows.texelWorld[2], shadows.texelWorld[3]);
    ubo.shadowDepthScale =
        glm::vec4(shadows.depthScale[0], shadows.depthScale[1], shadows.depthScale[2], shadows.depthScale[3]);
    ubo.shadowBias =
        glm::vec4(shadows.normalOffset, shadows.receiverBias, static_cast<float>(shadows.cascadeCount), 0.f);
    // Occlusion volume: the chunk grid, the window, the strengths, the volume texel and the slot
    // table. Without a volume — disabled (occlusionWindow.w = 0: the shader does not reach the
    // sample).
    const OcclusionVolumeData &occlusion = lighting.occlusion;
    const bool occlusionActive = occlusion.volume.get() != nullptr && occlusion.windowChunks > 0 &&
                                 (occlusion.aoStrength > 0.f || occlusion.sunStrengthFar > 0.f ||
                                  occlusion.sunStrengthNear > 0.f);
    ubo.occlusionGrid = glm::vec4(
        occlusion.cellSize, static_cast<float>(occlusion.chunkCells), static_cast<float>(occlusion.tileSize),
        static_cast<float>(occlusion.slices)
    );
    ubo.occlusionWindow = glm::vec4(
        static_cast<float>(occlusion.windowMin.x), static_cast<float>(occlusion.windowMin.y),
        static_cast<float>(occlusion.windowChunks), occlusionActive ? 1.f : 0.f
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
    // Environment map: x — exposure, y — the highest mip (blurring of reflections by roughness),
    // z — 1 if a map is set (otherwise the slot holds the built-in sky gradient, PBR takes no
    // reflections).
    TextureAsset *environmentMap = lighting.environment.map.get();
    float environmentMaxMip = 0.f;
    if (environmentMap) {
        const Size size = environmentMap->getSize();
        environmentMaxMip = std::floor(std::log2(std::max(1.f, std::max(size.x, size.y))));
    }
    ubo.environmentParams =
        glm::vec4(std::max(lighting.environment.intensity, 0.f), environmentMaxMip, environmentMap ? 1.f : 0.f, 0.f);
    ubo.occlusionFocus =
        glm::vec4(occlusion.focus.x, occlusion.focus.y, occlusion.fadeRadius, occlusion.transmittanceBlend);
    return ubo;
}

} // namespace Panda
