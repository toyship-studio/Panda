#pragma once

#include <Panda/ScriptEngine/ABI/ScriptManifest.hpp>

#include <cstdint>

// The audio function table for scripts — a named contribution table
// (ScriptHostApi::hostApiTable_Find("audio", AUDIO_HOST_API_VERSION)); the SDK side is
// Bamboo::AudioAPI and Bamboo::AudioSourceComponentAPI. Sources are entities with an
// AudioSourceComponent; a call on an entity without one, or outside a running world, does
// nothing and returns 0. A clip is the id of an audio clip asset (a field of the script); a voice
// is the weak handle of the mixer — an index and a generation —, and a call with the handle of a
// sound that is over does nothing and returns 0. Positions are world coordinates. Bus names are
// those of AudioEngine::BUS_NAMES. A layout change is a new version.
namespace Panda::Scripting {

constexpr const char *AUDIO_HOST_API_NAME = "audio";
constexpr uint32_t AUDIO_HOST_API_VERSION = 3; // 2: the playhead and the length of a source; 3: clips and voices

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

// How a clip is played (Bamboo::AudioPlayback): a plain struct that crosses the table as it is.
struct AudioPlaybackData final {
    const char *bus = "sfx";
    float volume = 1.f;
    float pitch = 1.f;
    uint32_t loop = 0;
    float fadeInSeconds = 0.f;
    float startSeconds = 0.f;
    uint32_t spatial = 0;
    float position[3] = {0.f, 0.f, 0.f};
    float minDistance = 1.f;
    float maxDistance = 50.f;
};
using AudioClipPlayFn =
    uint32_t (*)(Handle clip, const AudioPlaybackData *playback, uint32_t *outIndex, uint32_t *outGeneration);
using AudioClipGetLengthFn = uint32_t (*)(Handle clip, float *outSeconds);
using AudioVoiceStopFn = uint32_t (*)(uint32_t index, uint32_t generation, float fadeSeconds);
using AudioVoiceIsPlayingFn = uint32_t (*)(uint32_t index, uint32_t generation);
using AudioVoiceSetFlagFn = uint32_t (*)(uint32_t index, uint32_t generation, uint32_t value);
using AudioVoiceSetFloatFn = uint32_t (*)(uint32_t index, uint32_t generation, float value);
using AudioVoiceGetFloatFn = uint32_t (*)(uint32_t index, uint32_t generation, float *outValue);
using AudioVoiceSetPositionFn = uint32_t (*)(uint32_t index, uint32_t generation, const float *position3);

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
    // The playhead of the source's voice, in seconds of its clip: 0 when nothing plays. Setting it
    // moves a playing (or paused) voice there.
    AudioSourceGetFloatFn source_GetTime = nullptr;
    AudioSourceSetFloatFn source_SetTime = nullptr;
    // The length of the source's clip in seconds, playing or not; 0 — no clip.
    AudioSourceGetFloatFn source_GetLength = nullptr;
    AudioBusSetVolumeFn bus_SetVolume = nullptr;
    AudioBusGetVolumeFn bus_GetVolume = nullptr;
    AudioStopAllFn stopAll = nullptr;
    // v3: a clip played without a source, and the voice it plays in.
    AudioClipPlayFn clip_Play = nullptr;
    AudioClipGetLengthFn clip_GetLength = nullptr;
    AudioVoiceStopFn voice_Stop = nullptr;
    AudioVoiceIsPlayingFn voice_IsPlaying = nullptr;
    AudioVoiceSetFlagFn voice_SetPaused = nullptr;
    AudioVoiceSetFloatFn voice_SetVolume = nullptr;
    AudioVoiceSetFloatFn voice_SetPitch = nullptr;
    AudioVoiceSetPositionFn voice_SetPosition = nullptr;
    AudioVoiceGetFloatFn voice_GetTime = nullptr;
    AudioVoiceSetFloatFn voice_SetTime = nullptr;
};

} // namespace Panda::Scripting
