#include "Bamboo/WorldLightingAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace WorldLightingAPI {
    void setAmbientColor(Color color) {
        if (!Internal::requireMainThreadApi("WorldLightingAPI::setAmbientColor")) { return; }
        Panda::ExternalCalls::worldLighting_SetAmbientColor(color.r, color.g, color.b, color.a);
    }

    void setAmbientIntensity(float intensity) {
        if (!Internal::requireMainThreadApi("WorldLightingAPI::setAmbientIntensity")) { return; }
        Panda::ExternalCalls::worldLighting_SetAmbientIntensity(intensity);
    }

    void setEnvironmentIntensity(float intensity) {
        if (!Internal::requireMainThreadApi("WorldLightingAPI::setEnvironmentIntensity")) { return; }
        Panda::ExternalCalls::worldLighting_SetEnvironmentIntensity(intensity);
    }

    void setEnvironmentMap(TextureHandle map) {
        if (!Internal::requireMainThreadApi("WorldLightingAPI::setEnvironmentMap")) { return; }
        Panda::ExternalCalls::worldLighting_SetEnvironmentMap(map.id());
    }
} // namespace WorldLightingAPI

} // namespace Bamboo
