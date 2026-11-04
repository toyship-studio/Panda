#include "Panda/Audio/AudioExtensions.hpp"

#include "AudioSystem.hpp"
#include "Panda/Audio/AudioClipAsset.hpp"
#include "Panda/Audio/AudioComponents.hpp"
#include "Panda/Audio/AudioDtos.hpp"
#include "Panda/Audio/AudioEngine.hpp"

#include <Panda/Assets/Base/AssetRefImpl.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/Entity.hpp>

namespace Panda {

template class AssetRef<Audio::AudioClipAsset>;

namespace Audio {

    namespace {

        // --- Components.

        void encodeSource(Rain::Encoder &encoder, Entity entity) {
            const AudioSourceComponent &source = entity.getComponent<AudioSourceComponent>();
            AudioSourceComponentDto dto;
            dto.clip = source.clip.getId();
            dto.bus = source.bus;
            dto.volume = source.volume;
            dto.pitch = source.pitch;
            dto.loop = source.loop;
            dto.playOnStart = source.playOnStart;
            dto.spatial = source.spatial;
            dto.minDistance = source.minDistance;
            dto.maxDistance = source.maxDistance;
            dto.attenuation = static_cast<uint32_t>(source.attenuation);
            dto.dopplerFactor = source.dopplerFactor;
            encoder.encode("audioSourceComponent", dto);
        }

        void decodeSource(Rain::Decoder &decoder, Entity entity) {
            AudioSourceComponentDto dto;
            if (!decoder.decode("audioSourceComponent", dto)) { return; }
            AudioSourceComponent &source = entity.addComponent<AudioSourceComponent>();
            source.clip = AssetRef<Asset>(dto.clip);
            source.bus = dto.bus;
            source.volume = dto.volume;
            source.pitch = dto.pitch;
            source.loop = dto.loop;
            source.playOnStart = dto.playOnStart;
            source.spatial = dto.spatial;
            source.minDistance = dto.minDistance;
            source.maxDistance = dto.maxDistance;
            source.attenuation =
                dto.attenuation <= static_cast<uint32_t>(AttenuationModel::Exponential)
                    ? static_cast<AttenuationModel>(dto.attenuation)
                    : AttenuationModel::Inverse;
            source.dopplerFactor = dto.dopplerFactor;
        }

        // The listener is a tag: the key is present with `true`, or absent.
        void encodeListener(Rain::Encoder &encoder, Entity) {
            encoder.encode("audioListenerComponent", true);
        }
        void decodeListener(Rain::Decoder &decoder, Entity entity) {
            bool listener = false;
            if (decoder.decode("audioListenerComponent", listener) && listener) { entity.addTag<AudioListenerComponent>(); }
        }

        // --- The "audio" application system: the mixer lives with the application, not with a
        // world (the editor has two worlds and sounds survive a world switch).

        void startEngine() {
            AudioEngine::startup();
        }
        void tickEngine(double) {
            if (AudioEngine *engine = AudioEngine::instance()) { engine->update(); }
        }
        void stopEngine() {
            AudioEngine::shutdown();
        }

        AppSystem appSystem() {
            AppSystem system;
            system.name = "audio";
            system.start = startEngine;
            system.tick = tickEngine;
            system.stop = stopEngine;
            return system;
        }

    } // namespace

    void registerExtensions(Extensions &extensions) {
        extensions.components.add(componentType<AudioSourceComponent>("audioSourceComponent", encodeSource, decodeSource));
        extensions.components.add(
            componentType<AudioListenerComponent>("audioListenerComponent", encodeListener, decodeListener)
        );
        extensions.assetKinds.add(AudioClipKind::kind());
        extensions.worldSystems.add(worldSystem());
        extensions.appSystems.add(appSystem());
        extensions.scriptHostApis.add(scriptHostApiTable());
    }

} // namespace Audio

} // namespace Panda
