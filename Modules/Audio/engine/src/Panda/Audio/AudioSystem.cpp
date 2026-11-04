#include "AudioSystem.hpp"

#include "Panda/Audio/AudioClipAsset.hpp"
#include "Panda/Audio/AudioComponents.hpp"
#include "Panda/Audio/AudioEngine.hpp"
#include "Panda/Audio/AudioSources.hpp"

#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/World.hpp>

#include <unordered_map>
#include <vector>

namespace Panda::Audio {

namespace {

    // The state of the audio of one running world: which voice plays the source of which entity,
    // and the detached one-shots the world still owns (they stop with it). Created at start,
    // erased at stop; handles are weak, so the destructor touches nothing.
    struct AudioWorldState final {
        struct Source final {
            VoiceHandle voice;
            AudioSourceComponent applied; // the component values the voice was last given
            glm::vec3 lastPosition = glm::vec3(0.f);
            bool hasLastPosition = false;
        };
        std::unordered_map<UUID, Source> sources;
        std::vector<VoiceHandle> oneShots;
    };

    bool sameTunables(const AudioSourceComponent &a, const AudioSourceComponent &b) {
        return a.volume == b.volume && a.pitch == b.pitch && a.loop == b.loop && a.spatial == b.spatial &&
               a.minDistance == b.minDistance && a.maxDistance == b.maxDistance && a.attenuation == b.attenuation &&
               a.dopplerFactor == b.dopplerFactor;
    }

    glm::vec3 worldPosition(World &world, Entity entity) {
        return glm::vec3(world.getWorldSpaceTransformMatrix(entity) * glm::vec4(0.f, 0.f, 0.f, 1.f));
    }

    VoiceParams paramsOf(const AudioSourceComponent &source, const glm::vec3 &position) {
        VoiceParams params;
        params.bus = source.bus;
        params.volume = source.volume;
        params.pitch = source.pitch;
        params.loop = source.loop;
        params.spatial = source.spatial;
        params.position = position;
        params.minDistance = source.minDistance;
        params.maxDistance = source.maxDistance;
        params.attenuation = source.attenuation;
        params.dopplerFactor = source.dopplerFactor;
        return params;
    }

    AudioClipAsset *clipOf(const AudioSourceComponent &source) {
        return source.clip.as<AudioClipAsset>().get();
    }

    // Starts (or restarts) the voice of the entity's source from its component.
    VoiceHandle startVoice(World &world, AudioWorldState &state, Entity entity) {
        AudioEngine *engine = AudioEngine::instance();
        if (engine == nullptr || !entity.hasComponent<AudioSourceComponent>()) { return {}; }
        const AudioSourceComponent &source = entity.getComponent<AudioSourceComponent>();
        AudioWorldState::Source &slot = state.sources[entity.getId()];
        engine->stop(slot.voice);
        slot = AudioWorldState::Source{};
        AudioClipAsset *clip = clipOf(source);
        if (clip == nullptr) { return {}; }
        slot.lastPosition = worldPosition(world, entity);
        slot.hasLastPosition = true;
        slot.applied = source;
        slot.voice = engine->play(*clip, paramsOf(source, slot.lastPosition));
        return slot.voice;
    }

    void stopVoice(AudioWorldState &state, UUID id, float fadeSeconds) {
        auto it = state.sources.find(id);
        if (it == state.sources.end()) { return; }
        if (AudioEngine *engine = AudioEngine::instance()) { engine->stop(it->second.voice, fadeSeconds); }
        state.sources.erase(it);
    }

    // The listener: the first entity with an AudioListenerComponent, else the main camera.
    void updateListener(World &world, AudioEngine &engine) {
        Entity listener;
        auto view = world.getAllEntitiesWith<AudioListenerComponent, TransformComponent>();
        for (auto handle : view) {
            if (!world.isValidEntt(handle)) { continue; }
            listener = world.entity(handle);
            break;
        }
        if (!listener.isValid()) { listener = world.findMainCameraEntity(); }
        if (!listener.isValid()) { return; }
        const glm::mat4 transform = world.getWorldSpaceTransformMatrix(listener);
        const glm::vec3 position = glm::vec3(transform[3]);
        const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2]));
        const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
        engine.setListener(position, forward, up);
    }

    // --- The hooks of the running world.

    void start(World &world) {
        AudioWorldState &state = world.context<AudioWorldState>();
        auto view = world.getAllEntitiesWith<AudioSourceComponent>();
        for (auto handle : view) {
            if (!world.isValidEntt(handle)) { continue; }
            if (view.get<AudioSourceComponent>(handle).playOnStart) { startVoice(world, state, world.entity(handle)); }
        }
    }

    void stop(World &world) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        if (state == nullptr) { return; }
        if (AudioEngine *engine = AudioEngine::instance()) {
            for (auto &entry : state->sources) { engine->stop(entry.second.voice); }
            for (VoiceHandle voice : state->oneShots) { engine->stop(voice); }
        }
        world.eraseContext<AudioWorldState>();
    }

    void entityRemoved(World &world, Entity entity) {
        if (AudioWorldState *state = world.findContext<AudioWorldState>()) {
            stopVoice(*state, entity.getId(), AudioEngine::DEFAULT_STOP_FADE_SECONDS);
        }
    }

    void entityRestored(World &world, Entity entity) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        if (state == nullptr || !entity.hasComponent<AudioSourceComponent>()) { return; }
        if (entity.getComponent<AudioSourceComponent>().playOnStart) { startVoice(world, *state, entity); }
    }

    void componentsChanged(World &world, Entity entity, entt::id_type typeId) {
        if (typeId != entt::type_hash<AudioSourceComponent>::value()) { return; }
        AudioWorldState *state = world.findContext<AudioWorldState>();
        if (state == nullptr) { return; }
        if (!entity.hasComponent<AudioSourceComponent>()) {
            stopVoice(*state, entity.getId(), AudioEngine::DEFAULT_STOP_FADE_SECONDS);
        } else if (entity.getComponent<AudioSourceComponent>().playOnStart) {
            startVoice(world, *state, entity);
        }
    }

    // PostUpdate, after the scripts: the voices follow their components and entities, the
    // listener follows its entity.
    void postUpdate(World &world, double deltaTime) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        AudioEngine *engine = AudioEngine::instance();
        if (state == nullptr || engine == nullptr) { return; }
        updateListener(world, *engine);
        for (auto it = state->sources.begin(); it != state->sources.end();) {
            AudioWorldState::Source &slot = it->second;
            Entity entity = world.getById(it->first);
            if (!entity.isValid() || !entity.hasComponent<AudioSourceComponent>()) {
                engine->stop(slot.voice);
                it = state->sources.erase(it);
                continue;
            }
            if (!engine->isValid(slot.voice)) { // finished on its own
                it = state->sources.erase(it);
                continue;
            }
            const AudioSourceComponent &source = entity.getComponent<AudioSourceComponent>();
            const glm::vec3 position = worldPosition(world, entity);
            if (!sameTunables(source, slot.applied)) {
                engine->apply(slot.voice, paramsOf(source, position));
                slot.applied = source;
            }
            if (source.spatial) {
                glm::vec3 velocity(0.f);
                if (source.dopplerFactor > 0.f && slot.hasLastPosition && deltaTime > 0.0) {
                    velocity = (position - slot.lastPosition) / static_cast<float>(deltaTime);
                }
                engine->setPosition(slot.voice, position, velocity);
            }
            slot.lastPosition = position;
            slot.hasLastPosition = true;
            ++it;
        }
        std::erase_if(state->oneShots, [engine](VoiceHandle voice) { return !engine->isValid(voice); });
    }

} // namespace

WorldSystem worldSystem() {
    WorldSystem system;
    system.name = "audio";
    system.after = {"scripts"};
    system.postUpdate = postUpdate;
    system.start = start;
    system.stop = stop;
    system.entityRemoved = entityRemoved;
    system.entityRestored = entityRestored;
    system.componentsChanged = componentsChanged;
    return system;
}

namespace Sources {

    VoiceHandle play(World &world, Entity entity) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        if (state == nullptr || !entity.isValid()) { return {}; }
        return startVoice(world, *state, entity);
    }

    void stop(World &world, Entity entity, float fadeSeconds) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        if (state == nullptr || !entity.isValid()) { return; }
        stopVoice(*state, entity.getId(), fadeSeconds);
    }

    void setPaused(World &world, Entity entity, bool paused) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        AudioEngine *engine = AudioEngine::instance();
        if (state == nullptr || engine == nullptr || !entity.isValid()) { return; }
        auto it = state->sources.find(entity.getId());
        if (it != state->sources.end()) { engine->setPaused(it->second.voice, paused); }
    }

    bool isPlaying(World &world, Entity entity) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        AudioEngine *engine = AudioEngine::instance();
        if (state == nullptr || engine == nullptr || !entity.isValid()) { return false; }
        auto it = state->sources.find(entity.getId());
        return it != state->sources.end() && engine->isValid(it->second.voice) && !engine->isPaused(it->second.voice);
    }

    VoiceHandle playOneShot(World &world, Entity entity, const glm::vec3 *position, float volumeScale, float pitchScale) {
        AudioWorldState *state = world.findContext<AudioWorldState>();
        AudioEngine *engine = AudioEngine::instance();
        if (state == nullptr || engine == nullptr || !entity.isValid() || !entity.hasComponent<AudioSourceComponent>()) {
            return {};
        }
        const AudioSourceComponent &source = entity.getComponent<AudioSourceComponent>();
        AudioClipAsset *clip = clipOf(source);
        if (clip == nullptr) { return {}; }
        VoiceParams params = paramsOf(source, position != nullptr ? *position : worldPosition(world, entity));
        params.loop = false; // a one-shot ends by itself
        params.volume *= volumeScale;
        params.pitch *= pitchScale;
        const VoiceHandle voice = engine->play(*clip, params);
        if (voice.isValid()) { state->oneShots.push_back(voice); }
        return voice;
    }

} // namespace Sources

} // namespace Panda::Audio
