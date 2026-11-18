#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// The particle emitter of an entity (the Particle Emitter component in the inspector): its rate,
// lifetime, sizes, colors and texture are the component's; the script switches it and asks it
// for particles at once. Positions are world coordinates. A call on an entity without the
// component, or before the world runs, does nothing and returns false or zero.
namespace ParticleEmitterComponentAPI {
    // A part of the emitter's texture, in shares of it from its upper left corner: a cell of a
    // sheet of sprites, a bit of an atlas.
    struct TextureRect {
        float x = 0.f;
        float y = 0.f;
        float width = 1.f;
        float height = 1.f;
    };

    // Whether the emitter emits by its rate. The particles that are alive burn out in any case.
    void setPlaying(EntityHandle entity, bool playing);
    bool isPlaying(EntityHandle entity);
    // `count` particles at once, whatever the rate and whether the emitter plays: a puff, a
    // splash, the chips of what broke. They leave the entity (or `position`) as the particles
    // of the rate do — the cone, the speed, the lifetime of the component —, their colors are
    // those of the component times `tint` and each shows the part `rect` of the texture. The cap
    // of the emitter holds: the answer is how many were emitted. An emitter kept for bursts has
    // a rate of zero.
    int emit(EntityHandle entity, int count);
    int emitAt(EntityHandle entity, Vec3 position, int count, Vec4 tint = Vec4(1.f, 1.f, 1.f, 1.f), TextureRect rect = TextureRect());
} // namespace ParticleEmitterComponentAPI

} // namespace Bamboo
