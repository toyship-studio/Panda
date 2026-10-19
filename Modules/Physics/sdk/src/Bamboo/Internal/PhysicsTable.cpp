#include "Bamboo/Internal/PhysicsTable.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo::Internal {

const Panda::Scripting::PhysicsHostApi *physicsTable() {
    static const Panda::Scripting::PhysicsHostApi *cached = nullptr;
    if (cached == nullptr && Panda::ExternalCalls::hostApiTable_Find != nullptr) {
        cached = static_cast<const Panda::Scripting::PhysicsHostApi *>(Panda::ExternalCalls::hostApiTable_Find(
            Panda::Scripting::PHYSICS_HOST_API_NAME, Panda::Scripting::PHYSICS_HOST_API_VERSION
        ));
    }
    return cached;
}

} // namespace Bamboo::Internal
