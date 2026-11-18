#pragma once

#include "Bamboo/Base.hpp"

#include <cstdint>

namespace Bamboo {

// An audio clip of the project (.wav, .ogg, .mp3, .flac): a field of a script, chosen in the
// inspector. A std::vector of them is a list — a playlist, the variants of a footstep — and takes
// a folder of clips dropped on it. The cook of a game packs the clips its scripts are given.
BAMBOO_ASSET_HANDLE(AudioClipHandle, "audio");

// A sound that plays: what AudioAPI::play gives, to change it while it sounds and to stop it. It
// ends by itself (unless it loops), with stop, and with the world. The handle of a sound that is
// over names nothing: a call with it does nothing and answers zeros.
struct AudioVoice {
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;

    bool isValid() const {
        return index != 0xFFFFFFFFu;
    }
};

// How a clip is played.
struct AudioPlayback {
    const char *bus = "sfx"; // "music", "sfx", "ui": the bus of the mixer the sound goes to
    float volume = 1.f;      // 0..1
    float pitch = 1.f;       // > 0; 1 — as recorded
    bool loop = false;
    float fadeIn = 0.f;    // seconds the sound takes to come in
    float startTime = 0.f; // seconds into the clip it begins at
    // Heard from a place of the world: at its full volume within minDistance of the listener,
    // fading toward maxDistance, panned by the side it is on. Without it the sound is the
    // player's own — music, a button, a footstep.
    bool spatial = false;
    Vec3 position;
    float minDistance = 1.f;
    float maxDistance = 50.f;
};

// The mixer of the game: clips played by scripts, the buses and the stop of everything. Volumes
// are linear gains 0..1 (the master is the output, "music", "sfx" and "ui" mix into it); a game
// keeps the values of its settings menu itself and applies them at start. Without the Audio
// module in the game the calls do nothing and return false or 0.
namespace AudioAPI {
    // Plays a clip; any number of sounds of one clip overlap. An invalid voice — no such clip, or
    // every voice of the mixer is a loop.
    AudioVoice play(AudioClipHandle clip, const AudioPlayback &playback = AudioPlayback());
    // The length of a clip in seconds (the clip is read for it); 0 — no clip.
    float getLength(AudioClipHandle clip);

    // Fades out over fadeSeconds (0 — at once) and ends the sound.
    void stop(AudioVoice voice, float fadeSeconds = 0.02f);
    bool isPlaying(AudioVoice voice); // sounding, not paused and not over
    void setPaused(AudioVoice voice, bool paused);
    void setVolume(AudioVoice voice, float volume);
    void setPitch(AudioVoice voice, float pitch);
    void setPosition(AudioVoice voice, Vec3 position); // of a spatial sound
    // Where the sound is in its clip, in seconds; with getLength it tells how much is left — when
    // to bring the next piece of music in.
    float getTime(AudioVoice voice);
    void setTime(AudioVoice voice, float seconds);

    bool setBusVolume(const char *bus, float volume); // "master", "music", "sfx", "ui"
    float getBusVolume(const char *bus);
    // Every voice of the application fades out over fadeSeconds (0 — at once).
    void stopAll(float fadeSeconds = 0.02f);
} // namespace AudioAPI

} // namespace Bamboo
