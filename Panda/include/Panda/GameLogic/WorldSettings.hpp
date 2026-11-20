#pragma once

#include "Panda/Renderer/FogRenderer.hpp"
#include "Panda/Renderer/SsaoRenderer.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Renderer/CascadedShadowMap.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Renderer/SceneWind.hpp"

#include <Rain/Rain.hpp>

namespace Panda {

// The settings of the world the core owns: the sections "lighting", "graphics" and "wind" of a
// .world, the fields of one struct (WorldSettings, read through World::getSettings) in
// World::context next to the settings of the modules (the settings registry: settingsField).

struct WorldLightingSettings final : public Rain::Codable {
    Color ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    float ambientIntensity = 0.12f;
    // Environment map of the world (equirect texture, LDR or .hdr): distributed as the system
    // texture environmentMap — read by the default sky and by PBR reflections. Empty — the
    // built-in sky gradient, no reflections. Intensity — the exposure of the map (sky and
    // reflections together).
    AssetRef<Asset> environmentMap;
    float environmentIntensity = 1.f;

    RAIN_FIELDS_BEGIN(WorldLightingSettings)
    RAIN_FIELD(ambientColor)
    RAIN_FIELD(ambientIntensity)
    RAIN_FIELD(environmentMap)
    RAIN_FIELD(environmentIntensity)
    RAIN_FIELDS_END
};

// World graphics: the core's frame lighting — shadows, the occlusion volume, the screen-space
// ambient occlusion, the fog.
struct WorldGraphicsSettings final : public Rain::Codable {
    // Cascaded sun shadows: cascades/atlas tile/distance, strength and the biases against acne.
    CascadedShadowMap::Settings shadows;
    // Occlusion Volume: sky visibility and sun transmittance
    // from the flora scattering for all lit shaders.
    OcclusionVolume::Settings occlusion;
    // Screen-space ambient occlusion: the contact darkening of the ambient light.
    SsaoSettings ssao;
    // Fog: the air between the camera and what it sees.
    FogSettings fog;

    RAIN_FIELDS_BEGIN(WorldGraphicsSettings)
    RAIN_FIELD(shadows)
    RAIN_FIELD(occlusion)
    RAIN_FIELD(ssao)
    RAIN_FIELD(fog)
    RAIN_FIELDS_END
};

struct WorldSettings final {
    WorldLightingSettings lighting;
    WorldGraphicsSettings graphics;
    // The wind of every frame drawn of the world (WorldView::render applies it to SceneWind):
    // what is drawn in a frame of a world is drawn in the wind of that world.
    SceneWindSettings wind;
};

} // namespace Panda
