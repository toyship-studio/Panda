#include "Panda/GameLogic/Systems/LightingSystem.hpp"

#include "Panda/GameLogic/World.hpp"
#include "Panda/Renderer/PostprocessingPipeline.hpp" // projectSunToScreen
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <Foundation/Profile.hpp>

namespace Panda {

namespace {

    // The light sources of the frame from the components: the first enabled sun, point lights —
    // up to RENDERER_MAX_POINT_LIGHTS (the position comes from the world transform).
    WorldLightingData collectLights(World &world) {
        WorldLightingData lighting;
        const WorldLightingSettings &settings = world.getLightingSettings();
        lighting.ambientColor = settings.ambientColor;
        lighting.ambientIntensity = settings.ambientIntensity;
        lighting.environment.map = settings.environmentMap.as<TextureAsset>();
        lighting.environment.intensity = settings.environmentIntensity;

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

        auto points = world.getAllEntitiesWith<PointLightComponent, TransformComponent>();
        uint32_t pointLightCount = 0;
        for (auto entityHandle : points) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            if (pointLightCount >= RENDERER_MAX_POINT_LIGHTS) { break; }
            auto &lightComponent = points.get<PointLightComponent>(entityHandle);
            if (!lightComponent.isEnabled || lightComponent.intensity <= 0.f || lightComponent.radius <= 0.f) {
                continue;
            }
            const glm::mat4 worldTransform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            PointLightData &pointLight = lighting.pointLights[pointLightCount++];
            pointLight.position = glm::vec3(worldTransform * glm::vec4(0.f, 0.f, 0.f, 1.f));
            pointLight.radius = lightComponent.radius;
            pointLight.color = lightComponent.color;
            pointLight.intensity = lightComponent.intensity;
        }
        lighting.pointLightCount = pointLightCount;
        return lighting;
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
    m_data = collectLights(world);
    m_data.cameraPosition = cameraPosition;
    const DirectionalLightData &sun = m_data.directionalLight;
    // A point at infinity: behind the camera or far outside the frame z = 0, the flares are
    // turned off.
    m_sunScreen = sun.intensity > 0.f ? projectSunToScreen(viewProj, sun.direction) : glm::vec4(0.f);
    const WorldGraphicsSettings &graphics = world.getGraphicsSettings();
    // The shadow cascades are fitted to the camera and the sun of the frame; the depth passes go
    // after the scene traversal (the GPU executes them earlier — the views are above SCENE_VIEW).
    const glm::vec3 shadowLight = sun.castShadows && sun.intensity > 0.f ? sun.direction : glm::vec3(0.f);
    if (m_shadowMap.update(graphics.shadows, viewProj, cameraPosition, shadowLight)) {
        m_data.shadows = m_shadowMap.data();
    }
    // Occlusion volume: a window around the camera built from the flora scattering (rebaked in the
    // background on a window shift / a flora edit / a sun rotation), read by all lit shaders.
    m_occlusion.update(
        graphics.occlusion, cameraPosition, sun.intensity > 0.f ? sun.direction : glm::vec3(0.f, 1.f, 0.f),
        occluderVersion(world),
        [&world](const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out) {
            collectOccluders(world, minWorld, maxWorld, out);
        }
    );
    m_data.occlusion = m_occlusion.data();
    return m_data;
}

} // namespace Panda
