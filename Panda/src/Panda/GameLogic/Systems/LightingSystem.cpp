#include "Panda/GameLogic/Systems/LightingSystem.hpp"

#include "Panda/GameLogic/World.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/PostprocessingPipeline.hpp" // projectSunToScreen
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <vector>

namespace Panda {

namespace {

    // The lights of the frame: the ambient of the world settings; from the components — the first
    // enabled sun and the lamps (point lights and spots), up to RENDERER_MAX_POINT_LIGHTS of those
    // nearest to the camera (the position and the direction come from the world transform).
    void collectLights(World &world, const glm::vec3 &cameraPosition, WorldLightingData &lighting) {
        const WorldLightingSettings &settings = world.getLightingSettings();
        lighting.ambientColor = settings.ambientColor;
        lighting.ambientIntensity = settings.ambientIntensity;

        auto suns = world.getAllEntitiesWith<DirectionalLightComponent, TransformComponent>();
        for (auto entityHandle : suns) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &lightComponent = suns.get<DirectionalLightComponent>(entityHandle);
            if (!lightComponent.isEnabled || lightComponent.intensity <= 0.f) { continue; }
            const glm::mat4 worldTransform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.f, 0.f, -1.f, 0.f)));
            lighting.directionalLight.direction = -forward;
            lighting.directionalLight.color = lightComponent.color;
            lighting.directionalLight.intensity = lightComponent.intensity;
            lighting.directionalLight.castShadows = lightComponent.castShadows;
            break;
        }

        // The lamps: those that matter to the frame first — a lamp counts by how far past its
        // reach the camera stands — and no more than the block holds.
        struct Lamp {
            float distance = 0.f;
            PointLightData data;
        };
        std::vector<Lamp> lamps;
        auto points = world.getAllEntitiesWith<PointLightComponent, TransformComponent>();
        for (auto entityHandle : points) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &lightComponent = points.get<PointLightComponent>(entityHandle);
            if (!lightComponent.isEnabled || lightComponent.intensity <= 0.f || lightComponent.radius <= 0.f) {
                continue;
            }
            const glm::mat4 worldTransform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            Lamp lamp;
            lamp.data.position = glm::vec3(worldTransform * glm::vec4(0.f, 0.f, 0.f, 1.f));
            lamp.data.radius = lightComponent.radius;
            lamp.data.color = lightComponent.color;
            lamp.data.intensity = lightComponent.intensity;
            lamp.data.direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.f, 0.f, -1.f, 0.f)));
            lamp.data.coneAngle = lightComponent.coneAngle;
            lamp.data.coneSoftness = lightComponent.coneSoftness;
            lamp.data.castShadows = lightComponent.castShadows;
            lamp.distance = glm::distance(lamp.data.position, cameraPosition) - lamp.data.radius;
            lamps.push_back(lamp);
        }
        if (lamps.size() > RENDERER_MAX_POINT_LIGHTS) {
            std::partial_sort(
                lamps.begin(), lamps.begin() + RENDERER_MAX_POINT_LIGHTS, lamps.end(),
                [](const Lamp &a, const Lamp &b) { return a.distance < b.distance; }
            );
            lamps.resize(RENDERER_MAX_POINT_LIGHTS);
        }
        for (size_t i = 0; i < lamps.size(); ++i) { lighting.pointLights[i] = lamps[i].data; }
        lighting.pointLightCount = static_cast<uint32_t>(lamps.size());
    }

    // Occluders of the occlusion volume — contributions of the world systems
    // (WorldSystem::collectOccluders).
    void collectOccluders(World &world, const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out) {
        for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
            if (system.collectOccluders != nullptr) { system.collectOccluders(world, minWorld, maxWorld, out); }
        }
    }

    // Version of the occluder source: a change in any system rebakes the occlusion volume.
    uint64_t occluderVersion(World &world) {
        uint64_t version = 1469598103934665603ull;
        for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
            if (system.occluderVersion == nullptr) { continue; }
            version = (version ^ system.occluderVersion(world)) * 1099511628211ull;
        }
        return version;
    }

} // namespace

const WorldLightingData &
LightingSystem::update(World &world, const glm::mat4 &viewProj, const glm::vec3 &cameraPosition) {
    PANDA_PROFILE_SCOPE_N("World: lighting");
    // The shader features of the project (ShaderFeatures): each one fills its own data, and one
    // that is off is not collected, fitted or baked — its part stays the data of an empty scene.
    const ShaderFeatureSet &features = ShaderFeatures::enabled();
    m_data = WorldLightingData{};
    if (features.has(ShaderFeature::Lights)) {
        collectLights(world, cameraPosition, m_data);
    } else {
        // No lights in the project: the scene is unlit — a full white ambient shows the albedo,
        // as the lit shaders of the project do without the lights block (panda/lighting.hlsli).
        m_data.ambientColor = Color(1.f, 1.f, 1.f, 1.f);
        m_data.ambientIntensity = 1.f;
    }
    if (features.has(ShaderFeature::Environment)) {
        const WorldLightingSettings &settings = world.getLightingSettings();
        m_data.environment.map = settings.environmentMap.as<TextureAsset>();
        m_data.environment.intensity = settings.environmentIntensity;
        // The light of the sky for the lit shaders, baked when the world gets another map. A world
        // without a map has the built-in gradient for a sky, and that sky lights it the same way.
        m_environment.update(m_data.environment.map ? m_data.environment.map : StaticResources::defaultSkyTexture);
        m_data.environment.lighting = m_environment.atlas();
    }
    if (features.has(ShaderFeature::Ssao)) { m_data.ssao = m_screenOcclusion; }
    m_data.cameraPosition = cameraPosition;
    const DirectionalLightData &sun = m_data.directionalLight;
    // A point at infinity: behind the camera or far outside the frame z = 0, the flares are
    // turned off.
    m_sunScreen = sun.intensity > 0.f ? projectSunToScreen(viewProj, sun.direction) : glm::vec4(0.f);
    const WorldGraphicsSettings &graphics = world.getGraphicsSettings();
    // The shadow cascades are fitted to the camera and the sun of the frame; the view of the world
    // draws the depth passes next, before the scene.
    CascadedShadowMap::Settings shadowSettings = graphics.shadows;
    if (!features.has(ShaderFeature::Shadows)) { shadowSettings.enabled = false; }
    const glm::vec3 shadowLight = sun.castShadows && sun.intensity > 0.f ? sun.direction : glm::vec3(0.f);
    if (m_shadowMap.update(shadowSettings, viewProj, cameraPosition, shadowLight)) {
        m_data.shadows = m_shadowMap.data();
    }
    // The lamps that cast shadows get their tiles (the depth passes go with the cascades).
    m_lampShadowMap.update(
        shadowSettings.enabled, shadowSettings.lampTileSize, shadowSettings.normalOffset, shadowSettings.receiverBias,
        m_data
    );
    // Occlusion volume: a window around the camera built from the flora scattering (rebaked in the
    // background on a window shift / a flora edit / a sun rotation), read by all lit shaders.
    OcclusionVolume::Settings occlusionSettings = graphics.occlusion;
    if (!features.has(ShaderFeature::Occlusion)) { occlusionSettings.enabled = false; }
    m_occlusion.update(
        occlusionSettings, cameraPosition, sun.intensity > 0.f ? sun.direction : glm::vec3(0.f, 1.f, 0.f),
        occluderVersion(world),
        [&world](const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out) {
            collectOccluders(world, minWorld, maxWorld, out);
        }
    );
    m_data.occlusion = m_occlusion.data();
    return m_data;
}

} // namespace Panda
