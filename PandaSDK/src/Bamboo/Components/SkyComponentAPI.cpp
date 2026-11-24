#include "Bamboo/Components/SkyComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace SkyComponentAPI {
    void setAmbientColor(EntityHandle entity, Color color) {
        if (!Internal::requireMainThreadApi("SkyComponentAPI::setAmbientColor")) { return; }
        Panda::ExternalCalls::skyComponent_SetAmbientColor(entity.id, color.r, color.g, color.b, color.a);
    }

    void setAmbientIntensity(EntityHandle entity, float intensity) {
        if (!Internal::requireMainThreadApi("SkyComponentAPI::setAmbientIntensity")) { return; }
        Panda::ExternalCalls::skyComponent_SetAmbientIntensity(entity.id, intensity);
    }

    void setEnvironmentIntensity(EntityHandle entity, float intensity) {
        if (!Internal::requireMainThreadApi("SkyComponentAPI::setEnvironmentIntensity")) { return; }
        Panda::ExternalCalls::skyComponent_SetEnvironmentIntensity(entity.id, intensity);
    }

    void setEnvironmentMap(EntityHandle entity, TextureHandle map) {
        if (!Internal::requireMainThreadApi("SkyComponentAPI::setEnvironmentMap")) { return; }
        Panda::ExternalCalls::skyComponent_SetEnvironmentMap(entity.id, map.id());
    }

    void setMain(EntityHandle entity) {
        if (!Internal::requireMainThreadApi("SkyComponentAPI::setMain")) { return; }
        Panda::ExternalCalls::entity_SetMain(entity.id, "skyComponent");
    }
} // namespace SkyComponentAPI

} // namespace Bamboo
