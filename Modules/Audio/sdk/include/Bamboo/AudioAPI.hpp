#pragma once

namespace Bamboo {

// The mixer of the game: the buses and the stop of everything. Volumes are linear gains 0..1
// (the master is the output, "music", "sfx" and "ui" mix into it); a game keeps the values of
// its settings menu itself and applies them at start. Without the Audio module in the game the
// calls do nothing and return false or 0.
namespace AudioAPI {
    bool setBusVolume(const char *bus, float volume); // "master", "music", "sfx", "ui"
    float getBusVolume(const char *bus);
    // Every voice of the application fades out over fadeSeconds (0 — at once).
    void stopAll(float fadeSeconds = 0.02f);
} // namespace AudioAPI

} // namespace Bamboo
