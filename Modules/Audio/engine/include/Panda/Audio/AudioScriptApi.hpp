#pragma once

#include <Panda/ScriptEngine/ABI/ScriptManifest.hpp>

#include <cstdint>

// The audio function table for scripts — a named contribution table
// (ScriptHostApi::hostApiTable_Find("audio", AUDIO_HOST_API_VERSION)); the SDK side is
// Bamboo::AudioAPI and Bamboo::AudioSourceComponentAPI. Sources are entities with an
// AudioSourceComponent; a call on an entity without one, or outside a running world, does
// nothing and returns 0. Positions are world coordinates. Bus names are those of
// AudioEngine::BUS_NAMES. A layout change is a new version.
namespace Panda::Scripting {

constexpr const char *AUDIO_HOST_API_NAME = "audio";
constexpr uint32_t AUDIO_HOST_API_VERSION = 1;

using AudioSourcePlayFn = uint32_t (*)(Handle entityHandle);
using AudioSourceStopFn = uint32_t (*)(Handle entityHandle, float fadeSeconds);
using AudioSourceSetPausedFn = uint32_t (*)(Handle entityHandle, uint32_t paused);
using AudioSourceIsPlayingFn = uint32_t (*)(Handle entityHandle);
// position3 null — at the entity; the scales multiply the volume and the pitch of the component.
using AudioSourcePlayOneShotFn =
    uint32_t (*)(Handle entityHandle, const float *position3, float volumeScale, float pitchScale);
using AudioSourceSetFloatFn = uint32_t (*)(Handle entityHandle, float value);
using AudioSourceGetFloatFn = uint32_t (*)(Handle entityHandle, float *outValue);
using AudioBusSetVolumeFn = uint32_t (*)(const char *bus, float volume);
using AudioBusGetVolumeFn = uint32_t (*)(const char *bus, float *outVolume);
using AudioStopAllFn = void (*)(float fadeSeconds);

struct AudioHostApi final {
    uint32_t version = AUDIO_HOST_API_VERSION;
    AudioSourcePlayFn source_Play = nullptr;
    AudioSourceStopFn source_Stop = nullptr;
    AudioSourceSetPausedFn source_SetPaused = nullptr;
    AudioSourceIsPlayingFn source_IsPlaying = nullptr;
    AudioSourcePlayOneShotFn source_PlayOneShot = nullptr;
    AudioSourceSetFloatFn source_SetVolume = nullptr;
    AudioSourceGetFloatFn source_GetVolume = nullptr;
    AudioSourceSetFloatFn source_SetPitch = nullptr;
    AudioSourceGetFloatFn source_GetPitch = nullptr;
    AudioBusSetVolumeFn bus_SetVolume = nullptr;
    AudioBusGetVolumeFn bus_GetVolume = nullptr;
    AudioStopAllFn stopAll = nullptr;
};

} // namespace Panda::Scripting
