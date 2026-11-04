#include "Bamboo/Internal/AudioTable.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo::Internal {

const Panda::Scripting::AudioHostApi *audioTable() {
    static const Panda::Scripting::AudioHostApi *cached = nullptr;
    if (cached == nullptr && Panda::ExternalCalls::hostApiTable_Find != nullptr) {
        cached = static_cast<const Panda::Scripting::AudioHostApi *>(Panda::ExternalCalls::hostApiTable_Find(
            Panda::Scripting::AUDIO_HOST_API_NAME, Panda::Scripting::AUDIO_HOST_API_VERSION
        ));
    }
    return cached;
}

} // namespace Bamboo::Internal
