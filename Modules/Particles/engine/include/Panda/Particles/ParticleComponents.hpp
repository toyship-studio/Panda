#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Base/Base.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// Particle emitter: the settings are in the component (serialized into .world under the key
// particleEmitterComponent), the live particles are system state (World::context, not in the
// component).
struct ParticleEmitterComponent {
    bool playing = true;            // emission is running (live particles burn out in any case)
    float rate = 20.f;              // particles per second
    float lifetime = 2.f;           // particle lifetime, s
    float lifetimeVariation = 0.3f; // lifetime spread fraction (0..1)
    float speed = 2.f;              // initial speed along the emitter's +Y, m/s
    float speedVariation = 0.3f;    // speed spread fraction (0..1)
    float spreadDegrees = 25.f;     // half-angle of the spread cone around +Y
    glm::vec3 gravity{0.f, -1.f, 0.f}; // acceleration, m/s² (world or local — like the particles)
    float startSize = 0.25f;        // quad side at the start of life, m
    float endSize = 0.05f;          // and at the end
    Color startColor{1.f, 0.8f, 0.3f, 1.f};
    Color endColor{1.f, 0.2f, 0.05f, 0.f};
    uint32_t maxParticles = 500;    // cap on the emitter's live particles
    AssetRef<Asset> texture;        // the quad multiplies the color by the texture; empty — white
    bool worldSpace = true;         // particles live in the world (a moving emitter leaves a trail)
};

} // namespace Panda
