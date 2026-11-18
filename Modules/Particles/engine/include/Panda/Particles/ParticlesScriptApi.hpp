#pragma once

#include <Panda/ScriptEngine/ABI/ScriptManifest.hpp>

#include <cstdint>

// The particles function table for scripts — a named contribution table
// (ScriptHostApi::hostApiTable_Find("particles", PARTICLES_HOST_API_VERSION)); the SDK side is
// Bamboo::ParticleEmitterComponentAPI. Emitters are entities with a ParticleEmitterComponent; a
// call on an entity without one, or outside a running world, does nothing and returns 0.
// Positions are world coordinates. A layout change is a new version.
namespace Panda::Scripting {

constexpr const char *PARTICLES_HOST_API_NAME = "particles";
constexpr uint32_t PARTICLES_HOST_API_VERSION = 1;

// `count` particles at once, whatever the rate of the emitter. position3 null — at the entity;
// tint4 (r, g, b, a) null — the colors of the emitter as they are, else multiplied by it; rect4
// (x, y, width, height in shares of the texture, from its upper left corner) null — the whole
// texture. Returns how many were emitted: the cap of the emitter holds.
using ParticleEmitterEmitFn =
    uint32_t (*)(Handle entityHandle, uint32_t count, const float *position3, const float *tint4, const float *rect4);
using ParticleEmitterSetPlayingFn = uint32_t (*)(Handle entityHandle, uint32_t playing);
using ParticleEmitterIsPlayingFn = uint32_t (*)(Handle entityHandle);

struct ParticlesHostApi final {
    uint32_t version = PARTICLES_HOST_API_VERSION;
    ParticleEmitterEmitFn emitter_Emit = nullptr;
    ParticleEmitterSetPlayingFn emitter_SetPlaying = nullptr;
    ParticleEmitterIsPlayingFn emitter_IsPlaying = nullptr;
};

} // namespace Panda::Scripting
