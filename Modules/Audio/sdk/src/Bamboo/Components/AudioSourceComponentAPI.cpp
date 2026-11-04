#include "Bamboo/Components/AudioSourceComponentAPI.hpp"
#include "Bamboo/Internal/AudioTable.hpp"
#include "Bamboo/Internal/MainThread.hpp"

namespace Bamboo::AudioSourceComponentAPI {

namespace {
    const Panda::Scripting::AudioHostApi *table(const char *apiName) {
        if (!Internal::requireMainThreadApi(apiName)) { return nullptr; }
        return Internal::audioTable();
    }
} // namespace

bool play(EntityHandle entity) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::play");
    return api != nullptr && api->source_Play(entity.id) != 0;
}

void stop(EntityHandle entity, float fadeSeconds) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::stop")) {
        api->source_Stop(entity.id, fadeSeconds);
    }
}

void setPaused(EntityHandle entity, bool paused) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::setPaused")) {
        api->source_SetPaused(entity.id, paused ? 1 : 0);
    }
}

bool isPlaying(EntityHandle entity) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::isPlaying");
    return api != nullptr && api->source_IsPlaying(entity.id) != 0;
}

bool playOneShot(EntityHandle entity, float volumeScale, float pitchScale) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::playOneShot");
    return api != nullptr && api->source_PlayOneShot(entity.id, nullptr, volumeScale, pitchScale) != 0;
}

bool playOneShotAt(EntityHandle entity, Vec3 position, float volumeScale, float pitchScale) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::playOneShotAt");
    if (api == nullptr) { return false; }
    const float position3[3] = {position.x, position.y, position.z};
    return api->source_PlayOneShot(entity.id, position3, volumeScale, pitchScale) != 0;
}

void setVolume(EntityHandle entity, float volume) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::setVolume")) {
        api->source_SetVolume(entity.id, volume);
    }
}

float getVolume(EntityHandle entity) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::getVolume");
    float value = 0.f;
    if (api == nullptr || api->source_GetVolume(entity.id, &value) == 0) { return 0.f; }
    return value;
}

void setPitch(EntityHandle entity, float pitch) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::setPitch")) {
        api->source_SetPitch(entity.id, pitch);
    }
}

float getPitch(EntityHandle entity) {
    const Panda::Scripting::AudioHostApi *api = table("AudioSourceComponentAPI::getPitch");
    float value = 0.f;
    if (api == nullptr || api->source_GetPitch(entity.id, &value) == 0) { return 0.f; }
    return value;
}

} // namespace Bamboo::AudioSourceComponentAPI
