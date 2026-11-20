#include "Bamboo/Haptics.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace Haptics {
    void play(Haptic haptic) {
        if (Panda::ExternalCalls::haptics_Play == nullptr) { return; }
        Panda::ExternalCalls::haptics_Play(static_cast<uint32_t>(haptic));
    }
} // namespace Haptics

} // namespace Bamboo
