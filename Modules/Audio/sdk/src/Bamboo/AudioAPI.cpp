#include "Bamboo/AudioAPI.hpp"
#include "Bamboo/Internal/AudioTable.hpp"
#include "Bamboo/Internal/MainThread.hpp"

namespace Bamboo::AudioAPI {

bool setBusVolume(const char *bus, float volume) {
    if (!Internal::requireMainThreadApi("AudioAPI::setBusVolume")) { return false; }
    const Panda::Scripting::AudioHostApi *api = Internal::audioTable();
    return api != nullptr && api->bus_SetVolume(bus, volume) != 0;
}

float getBusVolume(const char *bus) {
    if (!Internal::requireMainThreadApi("AudioAPI::getBusVolume")) { return 0.f; }
    const Panda::Scripting::AudioHostApi *api = Internal::audioTable();
    float volume = 0.f;
    if (api == nullptr || api->bus_GetVolume(bus, &volume) == 0) { return 0.f; }
    return volume;
}

void stopAll(float fadeSeconds) {
    if (!Internal::requireMainThreadApi("AudioAPI::stopAll")) { return; }
    if (const Panda::Scripting::AudioHostApi *api = Internal::audioTable()) { api->stopAll(fadeSeconds); }
}

} // namespace Bamboo::AudioAPI
