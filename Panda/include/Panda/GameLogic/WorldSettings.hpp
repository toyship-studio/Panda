#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Renderer/CascadedShadowMap.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"

#include <Rain/Rain.hpp>

namespace Panda {

// The core settings of the world: sections of the world scope of the settings registry
// (`Extensions::settings`, the keys "lighting" and "graphics" of .world; the wind is
// SceneWindSettings under "wind") — in World::context like the settings of the modules, read
// through World::getLightingSettings / getGraphicsSettings / getWindSettings.

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

// World graphics: the core's frame lighting — shadows and the occlusion volume.
struct WorldGraphicsSettings final : public Rain::Codable {
    // Cascaded sun shadows: cascades/atlas tile/distance, strength and the biases against acne.
    CascadedShadowMap::Settings shadows;
    // Occlusion Volume: sky visibility and sun transmittance
    // from the flora scattering for all lit shaders.
    OcclusionVolume::Settings occlusion;

    RAIN_FIELDS_BEGIN(WorldGraphicsSettings)
    RAIN_FIELD(shadows)
    RAIN_FIELD(occlusion)
    RAIN_FIELDS_END
};

} // namespace Panda
