#include "Bamboo/AudioAPI.hpp"
#include "Bamboo/Internal/AudioTable.hpp"
#include "Bamboo/Internal/MainThread.hpp"

namespace Bamboo::AudioAPI {

namespace {
    const Panda::Scripting::AudioHostApi *table(const char *apiName) {
        if (!Internal::requireMainThreadApi(apiName)) { return nullptr; }
        return Internal::audioTable();
    }
} // namespace

AudioVoice play(AudioClipHandle clip, const AudioPlayback &playback) {
    const Panda::Scripting::AudioHostApi *api = table("AudioAPI::play");
    AudioVoice voice;
    if (api == nullptr || !clip.isValid()) { return voice; }
    Panda::Scripting::AudioPlaybackData data;
    data.bus = playback.bus;
    data.volume = playback.volume;
    data.pitch = playback.pitch;
    data.loop = playback.loop ? 1 : 0;
    data.fadeInSeconds = playback.fadeIn;
    data.startSeconds = playback.startTime;
    data.spatial = playback.spatial ? 1 : 0;
    data.position[0] = playback.position.x;
    data.position[1] = playback.position.y;
    data.position[2] = playback.position.z;
    data.minDistance = playback.minDistance;
    data.maxDistance = playback.maxDistance;
    uint32_t index = 0, generation = 0;
    if (api->clip_Play(clip.id(), &data, &index, &generation) == 0) { return voice; }
    voice.index = index;
    voice.generation = generation;
    return voice;
}

float getLength(AudioClipHandle clip) {
    const Panda::Scripting::AudioHostApi *api = table("AudioAPI::getLength");
    float seconds = 0.f;
    if (api == nullptr || !clip.isValid() || api->clip_GetLength(clip.id(), &seconds) == 0) { return 0.f; }
    return seconds;
}

void stop(AudioVoice voice, float fadeSeconds) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::stop")) {
        if (voice.isValid()) { api->voice_Stop(voice.index, voice.generation, fadeSeconds); }
    }
}

bool isPlaying(AudioVoice voice) {
    const Panda::Scripting::AudioHostApi *api = table("AudioAPI::isPlaying");
    return api != nullptr && voice.isValid() && api->voice_IsPlaying(voice.index, voice.generation) != 0;
}

void setPaused(AudioVoice voice, bool paused) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::setPaused")) {
        if (voice.isValid()) { api->voice_SetPaused(voice.index, voice.generation, paused ? 1 : 0); }
    }
}

void setVolume(AudioVoice voice, float volume) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::setVolume")) {
        if (voice.isValid()) { api->voice_SetVolume(voice.index, voice.generation, volume); }
    }
}

void setPitch(AudioVoice voice, float pitch) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::setPitch")) {
        if (voice.isValid()) { api->voice_SetPitch(voice.index, voice.generation, pitch); }
    }
}

void setPosition(AudioVoice voice, Vec3 position) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::setPosition")) {
        const float position3[3] = {position.x, position.y, position.z};
        if (voice.isValid()) { api->voice_SetPosition(voice.index, voice.generation, position3); }
    }
}

float getTime(AudioVoice voice) {
    const Panda::Scripting::AudioHostApi *api = table("AudioAPI::getTime");
    float seconds = 0.f;
    if (api == nullptr || !voice.isValid() || api->voice_GetTime(voice.index, voice.generation, &seconds) == 0) { return 0.f; }
    return seconds;
}

void setTime(AudioVoice voice, float seconds) {
    if (const Panda::Scripting::AudioHostApi *api = table("AudioAPI::setTime")) {
        if (voice.isValid()) { api->voice_SetTime(voice.index, voice.generation, seconds); }
    }
}

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
