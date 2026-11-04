#pragma once

#include "Panda/Audio/AudioClipAsset.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Panda::Audio {

// How the volume of a spatial voice falls with the distance to the listener. Persisted in .world
// as a number: the values never change.
enum class AttenuationModel : uint8_t { None = 0, Inverse = 1, Linear = 2, Exponential = 3 };

// A playing (or paused) sound of the mixer. Handles are weak: a voice that finished, was stopped
// or was stolen for a newer sound makes its handle stale, and every call with a stale handle is a
// no-op (isValid tells).
struct VoiceHandle final {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool isValid() const {
        return index != UINT32_MAX;
    }
    bool operator==(const VoiceHandle &other) const = default;
};

// Everything a voice is started with; the tunable part (volume, pitch, loop, the spatial fields)
// can be applied again to a playing voice with AudioEngine::apply.
struct VoiceParams final {
    std::string bus = "sfx"; // one of AudioEngine::BUS_NAMES; an unknown name plays on "sfx"
    float volume = 1.f;      // 0..1 linear gain, before the bus and the master
    float pitch = 1.f;       // > 0; 1 is the clip's own rate
    bool loop = false;
    bool spatial = false;             // attenuated and panned by the position relative to the listener
    glm::vec3 position = glm::vec3(0.f);
    glm::vec3 velocity = glm::vec3(0.f); // m/s, for the Doppler shift (dopplerFactor > 0)
    float minDistance = 1.f;             // full volume up to here
    float maxDistance = 50.f;            // the attenuation stops growing here (silent for Linear)
    float rolloff = 1.f;                 // the steepness of Inverse / Exponential
    AttenuationModel attenuation = AttenuationModel::Inverse;
    float dopplerFactor = 0.f; // 0 — no Doppler
    float fadeInSeconds = 0.f;
    bool startPaused = false; // the voice is created and set up but not started (resume with setPaused(false))
};

struct AudioEngineStats final {
    uint32_t activeVoices = 0;
    uint32_t voiceCapacity = 0;
    uint32_t cachedClips = 0; // clips registered with the mixer (decoded or decoding while playing)
    uint32_t sampleRate = 0;  // of the mixer (the device's)
    uint32_t channels = 0;
    bool deviceActive = false; // false — the null backend: everything works, nothing is heard
};

// The mixer of the application: one per process, started and stopped by the "audio" application
// system (Extensions::appSystems) and ticked every frame. Voices, buses, the listener and the
// cache of clip data live here; which voice belongs to which entity of which world is the
// business of the audio world system (AudioSystem). Everything is main-thread only; the audio
// thread is the device callback of miniaudio, and no code of the engine runs on it. The audio
// clock is the device's frame counter — never Application::getTime().
class AudioEngine final {
public:
    static constexpr uint32_t VOICE_CAPACITY = 64;
    // A clip shorter than this is decoded into PCM once (shared by its voices), a longer one is
    // decoded while it plays (music).
    static constexpr float DECODE_IN_MEMORY_SECONDS = 10.f;
    static constexpr float DEFAULT_STOP_FADE_SECONDS = 0.02f; // against clicks

    // The buses, by name: the master is the output, the others mix into it. Fixed for now; a
    // configurable set comes with the project settings of the module.
    static constexpr std::array<const char *, 4> BUS_NAMES = {"master", "music", "sfx", "ui"};

    // nullptr before the application system started and after it stopped.
    static AudioEngine *instance();
    // The application system: start opens the device (the null backend when there is none),
    // shutdown stops every voice and closes it.
    static void startup();
    static void shutdown();
    // Once a frame: finished voices are released, the cache drops the clips nobody holds.
    void update();

    // --- Voices ---
    VoiceHandle play(const AudioClipAsset &clip, const VoiceParams &params);
    // Fades out over fadeSeconds (0 — at once) and releases the voice.
    void stop(VoiceHandle voice, float fadeSeconds = DEFAULT_STOP_FADE_SECONDS);
    void setPaused(VoiceHandle voice, bool paused);
    // The tunable part of the params (bus, fade-in and startPaused are ignored).
    void apply(VoiceHandle voice, const VoiceParams &params);
    void setPosition(VoiceHandle voice, const glm::vec3 &position, const glm::vec3 &velocity = glm::vec3(0.f));
    void seek(VoiceHandle voice, float seconds);
    bool isValid(VoiceHandle voice) const;
    bool isPaused(VoiceHandle voice) const;
    float cursorSeconds(VoiceHandle voice) const; // 0 for a stale handle
    float lengthSeconds(VoiceHandle voice) const; // 0 for a stale handle or an endless stream
    void stopAll(float fadeSeconds = DEFAULT_STOP_FADE_SECONDS);

    // --- Listener (one): where spatial voices are heard from ---
    void setListener(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up);

    // --- Buses ---
    bool setBusVolume(std::string_view bus, float volume); // false — unknown bus
    float busVolume(std::string_view bus) const;           // 0 for an unknown bus
    // The editor's mute: silences the output without touching the master volume.
    void setMuted(bool muted);
    bool isMuted() const;

    AudioEngineStats stats() const;

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

private:
    AudioEngine();
    ~AudioEngine();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Panda::Audio
