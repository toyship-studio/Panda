#include "Bamboo/Components/DirectionalLightComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace DirectionalLightComponentAPI {
    void setColor(EntityHandle entity, Color color) {
        if (!Internal::requireMainThreadApi("DirectionalLightComponentAPI::setColor")) { return; }
        Panda::ExternalCalls::directionalLightComponent_SetColor(entity.id, color.r, color.g, color.b, color.a);
    }

    void setIntensity(EntityHandle entity, float intensity) {
        if (!Internal::requireMainThreadApi("DirectionalLightComponentAPI::setIntensity")) { return; }
        Panda::ExternalCalls::directionalLightComponent_SetIntensity(entity.id, intensity);
    }

    void setEnabled(EntityHandle entity, bool enabled) {
        if (!Internal::requireMainThreadApi("DirectionalLightComponentAPI::setEnabled")) { return; }
        Panda::ExternalCalls::directionalLightComponent_SetEnabled(entity.id, enabled);
    }
} // namespace DirectionalLightComponentAPI

} // namespace Bamboo
