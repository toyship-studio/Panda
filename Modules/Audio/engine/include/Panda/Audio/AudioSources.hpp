#pragma once

#include "Panda/Audio/AudioEngine.hpp"

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/GameLogic/Entity.hpp>

#include <glm/glm.hpp>

namespace Panda {

class World;

namespace Audio {

    // The audio sources of a RUNNING world, as the script host and the editor drive them: the
    // voice of an entity's AudioSourceComponent lives in the audio world system's state
    // (World::context<AudioWorldState>); outside a running world every call is a no-op that
    // returns false or an invalid handle. The world system itself (the hooks, the per-frame sync
    // of the components and the listener) is registered by the module: worldSystem().
    namespace Sources {
        // Starts the source's voice from its component (a playing one starts over).
        VoiceHandle play(World &world, Entity entity);
        void stop(World &world, Entity entity, float fadeSeconds = AudioEngine::DEFAULT_STOP_FADE_SECONDS);
        void setPaused(World &world, Entity entity, bool paused);
        bool isPlaying(World &world, Entity entity); // playing and not paused
        // The playhead of the source's voice, in seconds of its clip: 0 when nothing plays.
        float time(World &world, Entity entity);
        // Moves the playhead of a playing (or paused) voice; a source that does not play keeps silent.
        void setTime(World &world, Entity entity, float seconds);
        // The length of the source's clip in seconds, playing or not (the clip is loaded for it);
        // 0 — no clip.
        float length(Entity entity);
        // A detached voice with the source's settings: it does not follow the entity and does not
        // touch the source's own voice. position null — the entity's position now.
        VoiceHandle playOneShot(
            World &world, Entity entity, const glm::vec3 *position, float volumeScale = 1.f, float pitchScale = 1.f
        );
    } // namespace Sources

    // A clip played without a source: what a script asks for with a clip it holds (a field of
    // the script, a list of them — a playlist, the variants of a footstep). The voice belongs to
    // the running world and stops with it; it is driven by its handle through the mixer
    // (AudioEngine: stop, setVolume, setPaused, cursorSeconds, …).
    namespace Clips {
        // An invalid handle — no running world, no such clip, no free voice. startSeconds — where
        // in the clip the voice begins.
        VoiceHandle play(World &world, AssetId clip, const VoiceParams &params, float startSeconds = 0.f);
        // The length of a clip in seconds (the clip is loaded for it); 0 — no such clip.
        float length(AssetId clip);
    } // namespace Clips

} // namespace Audio

} // namespace Panda
