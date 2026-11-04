#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// The audio source of an entity (the Audio Source component in the inspector): its clip, bus and
// spatial settings are the component's; the script starts and stops it. Positions are world
// coordinates. A call on an entity without the component, or before the world runs, does nothing
// and returns false or zeros.
namespace AudioSourceComponentAPI {
    // Starts the source from its clip (a playing source starts over). false — no clip or no voice.
    bool play(EntityHandle entity);
    void stop(EntityHandle entity, float fadeSeconds = 0.02f);
    void setPaused(EntityHandle entity, bool paused);
    bool isPlaying(EntityHandle entity); // playing and not paused
    // A detached copy of the source's sound: at the entity (or at `position`), with the volume and
    // the pitch of the component times the scales; it ends by itself and does not touch the
    // source's own voice. For sounds that overlap — shots, footsteps, clicks.
    bool playOneShot(EntityHandle entity, float volumeScale = 1.f, float pitchScale = 1.f);
    bool playOneShotAt(EntityHandle entity, Vec3 position, float volumeScale = 1.f, float pitchScale = 1.f);
    // The volume (0..1) and the pitch (> 0) of the component; a playing voice follows at once.
    void setVolume(EntityHandle entity, float volume);
    float getVolume(EntityHandle entity);
    void setPitch(EntityHandle entity, float pitch);
    float getPitch(EntityHandle entity);
} // namespace AudioSourceComponentAPI

} // namespace Bamboo
