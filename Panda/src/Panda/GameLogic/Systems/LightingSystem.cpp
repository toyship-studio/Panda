#include "Panda/GameLogic/Systems/LightingSystem.hpp"

#include "Panda/GameLogic/World.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/PostprocessingPipeline.hpp" // projectSunToScreen
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/RenderingSettings.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <vector>

namespace Panda {

namespace {

    // The lights of the frame: the ambient of the main sky (none without a sky); the main sun,
    // when it is on; the lamps (point lights and spots), up to RENDERER_MAX_POINT_LIGHTS of those
    // nearest to the camera (the position and the direction come from the world transform).
    void collectLights(World &world, const glm::vec3 &cameraPosition, bool withLamps, WorldLightingData &lighting) {
        if (Entity sky = world.findMain<SkyComponent>(); sky.isValid()) {
            const SkyComponent &component = sky.getComponent<SkyComponent>();
            lighting.ambientColor = component.ambientColor;
            lighting.ambientIntensity = component.ambientIntensity;
        } else {
            lighting.ambientIntensity = 0.f;
        }

        if (Entity sun = world.findMain<DirectionalLightComponent>(); sun.isValid()) {
            const DirectionalLightComponent &light = sun.getComponent<DirectionalLightComponent>();
            if (light.isEnabled && light.intensity > 0.f) {
                const glm::mat4 worldTransform = world.getWorldSpaceTransformMatrix(sun);
                const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.f, 0.f, -1.f, 0.f)));
                lighting.directionalLight.direction = -forward;
                lighting.directionalLight.color = light.color;
                lighting.directionalLight.intensity = light.intensity;
                lighting.directionalLight.castShadows = light.castShadows;
            }
        }

        // A project without the `lamps` feature has none: its shaders do not read them.
        if (!withLamps) { return; }
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

    // Version of the occluder source: a change in any system rebakes the occlusion volume. 0 —
    // nothing of the world occludes (no system has a source in it): the volume is not made.
    uint64_t occluderVersion(World &world) {
        uint64_t version = 1469598103934665603ull;
        bool any = false;
        for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
            if (system.occluderVersion == nullptr) { continue; }
            const uint64_t source = system.occluderVersion(world);
            if (source == 0) { continue; }
            version = (version ^ source) * 1099511628211ull;
            any = true;
        }
        return !any ? 0 : version != 0 ? version : 1;
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
        collectLights(world, cameraPosition, features.has(ShaderFeature::Lamps), m_data);
    } else {
        // No lights in the project: the scene is unlit — a full white ambient shows the albedo,
        // as the lit shaders of the project do without the lights block (panda/lighting.hlsli).
        m_data.ambientColor = Color(1.f, 1.f, 1.f, 1.f);
        m_data.ambientIntensity = 1.f;
    }
    Entity mainSky = world.findMain<SkyComponent>();
    if (features.has(ShaderFeature::Environment) && mainSky.isValid()) {
        const SkyComponent &sky = mainSky.getComponent<SkyComponent>();
        m_data.environment.map = sky.environmentMap.as<TextureAsset>();
        m_data.environment.intensity = sky.environmentIntensity;
        // The light of the sky for the lit shaders, baked when the sky gets another map. A sky
        // without a map has the built-in gradient, and that sky lights the world the same way; a
        // world without a sky has no light of it.
        m_environment.update(m_data.environment.map ? m_data.environment.map : StaticResources::defaultSkyTexture);
        m_data.environment.lighting = m_environment.atlas();
    }
    if (features.has(ShaderFeature::Ssao)) { m_data.ssao = m_screenOcclusion; }
    m_data.cameraPosition = cameraPosition;
    const DirectionalLightData &sun = m_data.directionalLight;
    // A point at infinity: behind the camera or far outside the frame z = 0, the flares are
    // turned off.
    m_sunScreen = sun.intensity > 0.f ? projectSunToScreen(viewProj, sun.direction) : glm::vec4(0.f);
    // The shadow cascades are fitted to the camera and the sun of the frame; the view of the world
    // draws the depth passes next, before the scene. The maps are the project's, the look the sun's.
    TypeContext &project = GameContext::getProjectSettings();
    const ShadowSettings &shadows = project.get<ShadowSettings>();
    CascadedShadowMap::Settings shadowSettings;
    shadowSettings.enabled = features.has(ShaderFeature::Shadows);
    shadowSettings.cascadeCount = shadows.cascadeCount;
    shadowSettings.tileSize = shadows.tileSize;
    shadowSettings.distance = shadows.distance;
    shadowSettings.lampTileSize = shadows.lampTileSize;
    shadowSettings.slopeBias = shadows.slopeBias;
    shadowSettings.normalOffset = shadows.normalOffset;
    shadowSettings.receiverBias = shadows.receiverBias;
    if (Entity sun = world.findMain<DirectionalLightComponent>(); sun.isValid()) {
        const DirectionalLightComponent &light = sun.getComponent<DirectionalLightComponent>();
        shadowSettings.strength = light.shadowStrength;
        shadowSettings.softness = light.shadowSoftness;
    }
    const glm::vec3 shadowLight = sun.castShadows && sun.intensity > 0.f ? sun.direction : glm::vec3(0.f);
    if (m_shadowMap.update(shadowSettings, viewProj, cameraPosition, shadowLight)) {
        m_data.shadows = m_shadowMap.data();
    }
    // The lamps that cast shadows get their tiles (the depth passes go with the cascades).
    m_lampShadowMap.update(
        shadowSettings.enabled && features.has(ShaderFeature::Lamps), shadowSettings.lampTileSize, shadowSettings.normalOffset, shadowSettings.receiverBias,
        m_data
    );
    // Occlusion volume: a window around the camera built from the flora scattering (rebaked in the
    // background on a window shift / a flora edit / a sun rotation), read by all lit shaders.
    // A world where nothing occludes (no flora) has no volume: no bakes for an empty one.
    const uint64_t occluders = occluderVersion(world);
    OcclusionVolume::Settings occlusionSettings = project.get<OcclusionVolume::Settings>();
    occlusionSettings.enabled = features.has(ShaderFeature::Occlusion) && occluders != 0;
    m_occlusion.update(
        occlusionSettings, cameraPosition, sun.intensity > 0.f ? sun.direction : glm::vec3(0.f, 1.f, 0.f),
        occluders,
        [&world](const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out) {
            collectOccluders(world, minWorld, maxWorld, out);
        }
    );
    m_data.occlusion = m_occlusion.data();
    return m_data;
}

} // namespace Panda
