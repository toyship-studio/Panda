#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Serialization/GlmCoders.hpp"

#include <Rain/Rain.hpp>

#include <cstdint>

namespace Panda {

// Emitter DTO in .world (key particleEmitterComponent); the defaults mirror the component.
struct ParticleEmitterDto : public Rain::Codable {
    bool playing = true;
    float rate = 20.f;
    float lifetime = 2.f;
    float lifetimeVariation = 0.3f;
    float speed = 2.f;
    float speedVariation = 0.3f;
    float spreadDegrees = 25.f;
    glm::vec3 gravity{0.f, -1.f, 0.f};
    float startSize = 0.25f;
    float endSize = 0.05f;
    Color startColor{1.f, 0.8f, 0.3f, 1.f};
    Color endColor{1.f, 0.2f, 0.05f, 0.f};
    uint32_t maxParticles = 500;
    UUID texture = 0;
    bool worldSpace = true;

    RAIN_FIELDS_BEGIN(ParticleEmitterDto)
    RAIN_FIELD(playing)
    RAIN_FIELD(rate)
    RAIN_FIELD(lifetime)
    RAIN_FIELD(lifetimeVariation)
    RAIN_FIELD(speed)
    RAIN_FIELD(speedVariation)
    RAIN_FIELD(spreadDegrees)
    RAIN_FIELD(gravity)
    RAIN_FIELD(startSize)
    RAIN_FIELD(endSize)
    RAIN_FIELD(startColor)
    RAIN_FIELD(endColor)
    RAIN_FIELD(maxParticles)
    RAIN_FIELD(texture)
    RAIN_FIELD(worldSpace)
    RAIN_FIELDS_END
};

} // namespace Panda
