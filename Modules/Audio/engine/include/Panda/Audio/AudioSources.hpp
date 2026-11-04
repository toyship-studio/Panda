#pragma once

#include "Panda/Audio/AudioEngine.hpp"

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
        // A detached voice with the source's settings: it does not follow the entity and does not
        // touch the source's own voice. position null — the entity's position now.
        VoiceHandle playOneShot(
            World &world, Entity entity, const glm::vec3 *position, float volumeScale = 1.f, float pitchScale = 1.f
        );
    } // namespace Sources

} // namespace Audio

} // namespace Panda
