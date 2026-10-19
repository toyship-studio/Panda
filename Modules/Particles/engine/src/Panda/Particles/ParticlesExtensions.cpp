#include "Panda/Particles/ParticlesExtensions.hpp"
#include "Panda/Particles/ParticleComponents.hpp"
#include "Panda/Particles/ParticleDtos.hpp"
#include "Panda/Particles/ParticleSystem.hpp"
#include "Panda/Particles/ParticlesResources.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/Entity.hpp>

namespace Panda::Particles {

namespace {

    void encodeEmitter(Rain::Encoder &encoder, Entity entity) {
        const ParticleEmitterComponent &emitter = entity.getComponent<ParticleEmitterComponent>();
        ParticleEmitterDto dto;
        dto.playing = emitter.playing;
        dto.rate = emitter.rate;
        dto.lifetime = emitter.lifetime;
        dto.lifetimeVariation = emitter.lifetimeVariation;
        dto.speed = emitter.speed;
        dto.speedVariation = emitter.speedVariation;
        dto.spreadDegrees = emitter.spreadDegrees;
        dto.gravity = emitter.gravity;
        dto.startSize = emitter.startSize;
        dto.endSize = emitter.endSize;
        dto.startColor = emitter.startColor;
        dto.endColor = emitter.endColor;
        dto.maxParticles = emitter.maxParticles;
        dto.texture = emitter.texture.getId();
        dto.worldSpace = emitter.worldSpace;
        encoder.encode("particleEmitterComponent", dto);
    }

    void decodeEmitter(Rain::Decoder &decoder, Entity entity) {
        ParticleEmitterDto dto;
        if (!decoder.decode("particleEmitterComponent", dto)) { return; }
        ParticleEmitterComponent &emitter = entity.addComponent<ParticleEmitterComponent>();
        emitter.playing = dto.playing;
        emitter.rate = dto.rate;
        emitter.lifetime = dto.lifetime;
        emitter.lifetimeVariation = dto.lifetimeVariation;
        emitter.speed = dto.speed;
        emitter.speedVariation = dto.speedVariation;
        emitter.spreadDegrees = dto.spreadDegrees;
        emitter.gravity = dto.gravity;
        emitter.startSize = dto.startSize;
        emitter.endSize = dto.endSize;
        emitter.startColor = dto.startColor;
        emitter.endColor = dto.endColor;
        emitter.maxParticles = dto.maxParticles;
        emitter.texture = AssetRef<Asset>(dto.texture);
        emitter.worldSpace = dto.worldSpace;
    }

} // namespace

void registerExtensions(Extensions &extensions) {
    extensions.components.add(
        componentType<ParticleEmitterComponent>("particleEmitterComponent", encodeEmitter, decodeEmitter)
    );
    extensions.worldSystems.add(worldSystem());
    extensions.resourceProviders.add(Resources::provider());
}

} // namespace Panda::Particles
