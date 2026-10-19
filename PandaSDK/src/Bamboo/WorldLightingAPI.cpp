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
} // namespace WorldLightingAPI

} // namespace Bamboo
