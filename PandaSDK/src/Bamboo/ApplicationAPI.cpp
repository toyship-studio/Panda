#include "Bamboo/ApplicationAPI.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace ApplicationAPI {
    uint32_t getWidth() {
        return Panda::ExternalCalls::application_GetWidth();
    }

    uint32_t getHeight() {
        return Panda::ExternalCalls::application_GetHeight();
    }

    float getTime() {
        return Panda::ExternalCalls::application_GetTime();
    }

    bool isCursorLocked() {
        return Panda::ExternalCalls::application_IsCursorLocked();
    }

    void setCursorLocked(bool locked) {
        Panda::ExternalCalls::application_SetCursorLocked(locked);
    }

    void quit() {
        Panda::ExternalCalls::application_Quit();
    }

    std::string getPersistentDataPath() {
        if (Panda::ExternalCalls::application_GetPersistentDataPath == nullptr) { return {}; }
        const uint32_t length = Panda::ExternalCalls::application_GetPersistentDataPath(nullptr, 0);
        if (length == 0) { return {}; }
        std::string path(length, '\0');
        Panda::ExternalCalls::application_GetPersistentDataPath(path.data(), length + 1);
        return path;
    }
} // namespace ApplicationAPI

} // namespace Bamboo
