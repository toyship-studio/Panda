#include "AudioSystem.hpp"

#include "Panda/Audio/AudioComponents.hpp"
#include "Panda/Audio/AudioEngine.hpp"
#include "Panda/Audio/AudioScriptApi.hpp"
#include "Panda/Audio/AudioSources.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/InnerScriptHook.hpp> // resolveScriptEntity

#include <algorithm>

namespace Panda::Audio {

namespace {

    using Scripting::Handle;

    // The entity of a handle together with the world the scripts run in; an entity without a
    // source, or no running world, is "nothing to do" (the calls return 0 and zeros).
    struct Target final {
        World *world = nullptr;
        Entity entity;

        bool valid() const {
            return world != nullptr && entity.isValid() && entity.hasComponent<AudioSourceComponent>();
        }
    };

    Target resolve(Handle entityHandle) {
        Target target;
        target.world = GameContext::getCurrentWorld();
        target.entity = resolveScriptEntity(entityHandle); // invalid + a one-time error for a broken handle
        return target;
    }

    uint32_t sourcePlay(Handle entityHandle) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        return Sources::play(*target.world, target.entity).isValid() ? 1 : 0;
    }

    uint32_t sourceStop(Handle entityHandle, float fadeSeconds) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        Sources::stop(*target.world, target.entity, std::max(fadeSeconds, 0.f));
        return 1;
    }

    uint32_t sourceSetPaused(Handle entityHandle, uint32_t paused) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        Sources::setPaused(*target.world, target.entity, paused != 0);
        return 1;
    }

    uint32_t sourceIsPlaying(Handle entityHandle) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        return Sources::isPlaying(*target.world, target.entity) ? 1 : 0;
    }

    uint32_t sourcePlayOneShot(Handle entityHandle, const float *position3, float volumeScale, float pitchScale) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        glm::vec3 position(0.f);
        if (position3 != nullptr) { position = glm::vec3(position3[0], position3[1], position3[2]); }
        return Sources::playOneShot(
                   *target.world, target.entity, position3 != nullptr ? &position : nullptr, volumeScale, pitchScale
        )
                       .isValid()
                   ? 1
                   : 0;
    }

    // The volume and the pitch live in the component: the audio system pushes them into the voice
    // at the end of the frame, and the world file keeps what the script set.
    uint32_t sourceSetVolume(Handle entityHandle, float value) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        target.entity.getComponent<AudioSourceComponent>().volume = std::max(value, 0.f);
        return 1;
    }

    uint32_t sourceGetVolume(Handle entityHandle, float *outValue) {
        const Target target = resolve(entityHandle);
        if (!target.valid() || outValue == nullptr) { return 0; }
        *outValue = target.entity.getComponent<AudioSourceComponent>().volume;
        return 1;
    }

    uint32_t sourceSetPitch(Handle entityHandle, float value) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        target.entity.getComponent<AudioSourceComponent>().pitch = std::max(value, 0.01f);
        return 1;
    }

    uint32_t sourceGetPitch(Handle entityHandle, float *outValue) {
        const Target target = resolve(entityHandle);
        if (!target.valid() || outValue == nullptr) { return 0; }
        *outValue = target.entity.getComponent<AudioSourceComponent>().pitch;
        return 1;
    }

    uint32_t busSetVolume(const char *bus, float volume) {
        AudioEngine *engine = AudioEngine::instance();
        if (engine == nullptr || bus == nullptr) { return 0; }
        return engine->setBusVolume(bus, volume) ? 1 : 0;
    }

    uint32_t busGetVolume(const char *bus, float *outVolume) {
        AudioEngine *engine = AudioEngine::instance();
        if (engine == nullptr || bus == nullptr || outVolume == nullptr) { return 0; }
        *outVolume = engine->busVolume(bus);
        return 1;
    }

    void stopAll(float fadeSeconds) {
        if (AudioEngine *engine = AudioEngine::instance()) { engine->stopAll(std::max(fadeSeconds, 0.f)); }
    }

    const Scripting::AudioHostApi s_table{
        .version = Scripting::AUDIO_HOST_API_VERSION,
        .source_Play = sourcePlay,
        .source_Stop = sourceStop,
        .source_SetPaused = sourceSetPaused,
        .source_IsPlaying = sourceIsPlaying,
        .source_PlayOneShot = sourcePlayOneShot,
        .source_SetVolume = sourceSetVolume,
        .source_GetVolume = sourceGetVolume,
        .source_SetPitch = sourceSetPitch,
        .source_GetPitch = sourceGetPitch,
        .bus_SetVolume = busSetVolume,
        .bus_GetVolume = busGetVolume,
        .stopAll = stopAll,
    };

} // namespace

ScriptHostApiTable scriptHostApiTable() {
    return {Scripting::AUDIO_HOST_API_NAME, Scripting::AUDIO_HOST_API_VERSION, &s_table};
}

} // namespace Panda::Audio
