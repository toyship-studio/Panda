#include "Bamboo/Components/PointLightComponentAPI.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace PointLightComponentAPI {
    void set(EntityHandle entity, Color color, float intensity, float radius, bool enabled) {
        Panda::ExternalCalls::pointLightComponent_Set(entity.id, color.r, color.g, color.b, intensity, radius, enabled);
    }
} // namespace PointLightComponentAPI

} // namespace Bamboo
