#include "Panda/Particles/ParticleSystem.hpp"

#include "Panda/Particles/ParticleComponents.hpp"
#include "Panda/Particles/ParticlesScriptApi.hpp"

#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/InnerScriptHook.hpp> // resolveScriptEntity

namespace Panda::Particles {

namespace {

    using Scripting::Handle;

    // The entity of a handle together with the world the scripts run in; an entity without an
    // emitter, or no running world, is "nothing to do" (the calls return 0).
    struct Target final {
        World *world = nullptr;
        Entity entity;

        bool valid() const {
            return world != nullptr && entity.isValid() && entity.hasComponent<ParticleEmitterComponent>();
        }
    };

    Target resolve(Handle entityHandle) {
        Target target;
        target.world = GameContext::getRunningWorld();
        target.entity = resolveScriptEntity(entityHandle); // invalid + a one-time error for a broken handle
        return target;
    }

    uint32_t emitterEmit(Handle entityHandle, uint32_t count, const float *position3, const float *tint4, const float *rect4) {
        const Target target = resolve(entityHandle);
        if (!target.valid() || count == 0) { return 0; }
        glm::vec3 position(0.f);
        if (position3 != nullptr) { position = glm::vec3(position3[0], position3[1], position3[2]); }
        const glm::vec4 tint = tint4 != nullptr ? glm::vec4(tint4[0], tint4[1], tint4[2], tint4[3]) : glm::vec4(1.f);
        const glm::vec4 rect = rect4 != nullptr ? glm::vec4(rect4[0], rect4[1], rect4[2], rect4[3]) : glm::vec4(0.f, 0.f, 1.f, 1.f);
        return emit(*target.world, target.entity, count, position3 != nullptr ? &position : nullptr, tint, rect);
    }

    // Whether the emitter emits by its rate lives in the component: the system reads it every
    // frame, and the particles that are alive burn out in any case.
    uint32_t emitterSetPlaying(Handle entityHandle, uint32_t playing) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        target.entity.getComponent<ParticleEmitterComponent>().playing = playing != 0;
        return 1;
    }

    uint32_t emitterIsPlaying(Handle entityHandle) {
        const Target target = resolve(entityHandle);
        if (!target.valid()) { return 0; }
        return target.entity.getComponent<ParticleEmitterComponent>().playing ? 1 : 0;
    }

    const Scripting::ParticlesHostApi s_table{
        .version = Scripting::PARTICLES_HOST_API_VERSION,
        .emitter_Emit = emitterEmit,
        .emitter_SetPlaying = emitterSetPlaying,
        .emitter_IsPlaying = emitterIsPlaying,
    };

} // namespace

ScriptHostApiTable scriptHostApiTable() {
    return {Scripting::PARTICLES_HOST_API_NAME, Scripting::PARTICLES_HOST_API_VERSION, &s_table};
}

} // namespace Panda::Particles
