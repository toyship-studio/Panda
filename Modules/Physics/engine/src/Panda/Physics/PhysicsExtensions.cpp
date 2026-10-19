#include "Panda/Physics/PhysicsExtensions.hpp"

#include <Panda/Extensions/Extensions.hpp>

namespace Panda::Physics {

void registerExtensions(Extensions &extensions) {
    registerComponentTypes(extensions.components);
    registerSystems(extensions.worldSystems);
    registerScriptApi(extensions.scriptHostApis);
}

} // namespace Panda::Physics
