#include "Bamboo/Internal/ParticlesTable.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo::Internal {

const Panda::Scripting::ParticlesHostApi *particlesTable() {
    static const Panda::Scripting::ParticlesHostApi *cached = nullptr;
    if (cached == nullptr && Panda::ExternalCalls::hostApiTable_Find != nullptr) {
        cached = static_cast<const Panda::Scripting::ParticlesHostApi *>(Panda::ExternalCalls::hostApiTable_Find(
            Panda::Scripting::PARTICLES_HOST_API_NAME, Panda::Scripting::PARTICLES_HOST_API_VERSION
        ));
    }
    return cached;
}

} // namespace Bamboo::Internal
