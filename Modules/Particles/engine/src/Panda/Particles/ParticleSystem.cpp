#include "Panda/Particles/ParticleSystem.hpp"
#include "Panda/Particles/ParticleComponents.hpp"
#include "Panda/Particles/ParticlesResources.hpp"

#include <Foundation/Profile.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/Renderer3D.hpp>
#include <Panda/Renderer/RenderingMode.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Panda::Particles {

namespace {

    struct Particle {
        glm::vec3 position; // world or the emitter's local space (worldSpace)
        glm::vec3 velocity;
        float age = 0.f;
        float lifetime = 1.f;
    };

    // Live particles of one emitter and its GPU instance buffer.
    struct EmitterState {
        std::vector<Particle> particles;
        float emitAccumulator = 0.f;
        uint32_t seed = 1;
        Miren::VertexBufferHandle instanceBuffer{MIREN_INVALID_HANDLE};
        uint32_t instanceCapacity = 0;

        ~EmitterState() {
            if (instanceBuffer.isValid()) { Miren::deleteVertexBuffer(instanceBuffer); }
        }
    };

    // Per-world system state (World::context): emitters by entity id, its own clock.
    struct ParticlesState {
        std::unordered_map<UUID, EmitterState> emitters;
        double lastTime = -1.0;
    };

    struct InstanceData {
        glm::vec4 columns[4];
        glm::vec4 color;
    };

    float random01(uint32_t &seed) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFFFFu) / 16777216.f;
    }

    // Random direction in a cone of half-angle spread around axis (uniform over the solid angle).
    glm::vec3 randomDirection(uint32_t &seed, const glm::vec3 &axis, float spreadRadians) {
        const float cosSpread = std::cos(spreadRadians);
        const float z = cosSpread + (1.f - cosSpread) * random01(seed);
        const float phi = random01(seed) * glm::two_pi<float>();
        const float r = std::sqrt(std::max(0.f, 1.f - z * z));
        const glm::vec3 local(r * std::cos(phi), z, r * std::sin(phi)); // around +Y
        const glm::vec3 up = glm::normalize(axis);
        const glm::vec3 helper = std::abs(up.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
        const glm::vec3 right = glm::normalize(glm::cross(helper, up));
        const glm::vec3 forward = glm::cross(up, right);
        return glm::normalize(right * local.x + up * local.y + forward * local.z);
    }

    double now() {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    void simulate(
        EmitterState &state, const ParticleEmitterComponent &emitter, const glm::mat4 &model, float dt
    ) {
        const uint32_t maxParticles = std::max(1u, emitter.maxParticles);
        // Death and integration.
        std::vector<Particle> &particles = state.particles;
        for (Particle &particle : particles) {
            particle.age += dt;
            particle.velocity += emitter.gravity * dt;
            particle.position += particle.velocity * dt;
        }
        particles.erase(
            std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.age >= p.lifetime; }),
            particles.end()
        );
        if (particles.size() > maxParticles) { particles.resize(maxParticles); }
        // Emission: rate particles per second, the fractional remainder accumulates.
        if (!emitter.playing || emitter.rate <= 0.f) {
            state.emitAccumulator = 0.f;
            return;
        }
        state.emitAccumulator += emitter.rate * dt;
        const glm::vec3 origin = emitter.worldSpace ? glm::vec3(model[3]) : glm::vec3(0.f);
        const glm::vec3 axis = emitter.worldSpace ? glm::vec3(model * glm::vec4(0.f, 1.f, 0.f, 0.f)) : glm::vec3(0.f, 1.f, 0.f);
        const glm::vec3 safeAxis = glm::length(axis) > 1.e-6f ? axis : glm::vec3(0.f, 1.f, 0.f);
        while (state.emitAccumulator >= 1.f && particles.size() < maxParticles) {
            state.emitAccumulator -= 1.f;
            Particle particle;
            particle.position = origin;
            const float speed = emitter.speed * (1.f + emitter.speedVariation * (random01(state.seed) * 2.f - 1.f));
            particle.velocity = randomDirection(state.seed, safeAxis, glm::radians(emitter.spreadDegrees)) * speed;
            particle.lifetime =
                std::max(0.05f, emitter.lifetime * (1.f + emitter.lifetimeVariation * (random01(state.seed) * 2.f - 1.f)));
            particles.push_back(particle);
        }
        if (state.emitAccumulator > 1.f) { state.emitAccumulator = 1.f; } // cap reached — do not build up a burst
    }

    void submitEmitter(
        Renderer3D &renderer, EmitterState &state, const ParticleEmitterComponent &emitter, const glm::mat4 &model,
        const glm::vec3 &cameraPosition
    ) {
        if (state.particles.empty() || !Resources::quadMesh || !Resources::instanceLayout.isValid()) { return; }
        std::vector<InstanceData> instances;
        instances.reserve(state.particles.size());
        const glm::vec3 worldUp(0.f, 1.f, 0.f);
        for (const Particle &particle : state.particles) {
            const float t = glm::clamp(particle.age / particle.lifetime, 0.f, 1.f);
            const float size = glm::mix(emitter.startSize, emitter.endSize, t);
            const glm::vec3 worldPos =
                emitter.worldSpace ? particle.position : glm::vec3(model * glm::vec4(particle.position, 1.f));
            // Quad facing the camera: forward — toward the camera, right/up — a basis in its plane.
            glm::vec3 forward = cameraPosition - worldPos;
            forward = glm::length(forward) > 1.e-6f ? glm::normalize(forward) : glm::vec3(0.f, 0.f, 1.f);
            const glm::vec3 helper = std::abs(forward.y) < 0.99f ? worldUp : glm::vec3(1.f, 0.f, 0.f);
            const glm::vec3 right = glm::normalize(glm::cross(helper, forward));
            const glm::vec3 up = glm::cross(forward, right);
            InstanceData instance;
            instance.columns[0] = glm::vec4(right * size, 0.f);
            instance.columns[1] = glm::vec4(up * size, 0.f);
            instance.columns[2] = glm::vec4(forward * size, 0.f);
            instance.columns[3] = glm::vec4(worldPos, 1.f);
            const Color &a = emitter.startColor;
            const Color &b = emitter.endColor;
            instance.color = glm::mix(glm::vec4(a.r, a.g, a.b, a.a), glm::vec4(b.r, b.g, b.b, b.a), t);
            instances.push_back(instance);
        }
        const uint32_t count = static_cast<uint32_t>(instances.size());
        const uint32_t bytes = count * static_cast<uint32_t>(sizeof(InstanceData));
        if (!state.instanceBuffer.isValid() || state.instanceCapacity < count) {
            if (state.instanceBuffer.isValid()) { Miren::deleteVertexBuffer(state.instanceBuffer); }
            state.instanceCapacity = std::max(count, std::max(64u, emitter.maxParticles));
            state.instanceBuffer = Miren::createDynamicVertexBuffer(
                Foundation::Memory::alloc(state.instanceCapacity * sizeof(InstanceData)),
                state.instanceCapacity * static_cast<uint32_t>(sizeof(InstanceData)), Resources::instanceLayout
            );
        }
        Miren::updateDynamicVertexBuffer(state.instanceBuffer, Foundation::Memory::copying(instances.data(), bytes), bytes);
        // The emitter's texture is a runtime slot of the shared material (the value goes into the
        // draw state on submit).
        AssetRef<Asset> texture = emitter.texture.isValid() ? emitter.texture : StaticResources::whiteTexture.asBaseAsset();
        Resources::material->setRuntimeField("baseColorTexture", texture);
        glm::mat4 identity(1.f); // the instances are already in world space
        renderer.submitInstanced(
            identity, *Resources::quadMesh, Resources::material, state.instanceBuffer, Resources::instanceLayout, count
        );
    }

    void submit(World &world, Renderer3D &renderer, const glm::vec3 &cameraPosition, const glm::mat4 &) {
        if (renderer.getMode() != RenderingMode::DEFAULT) { return; } // neither shadows nor picking
        PANDA_PROFILE_SCOPE_N("World: particles");
        ParticlesState &state = world.context<ParticlesState>();
        const double time = now();
        const float dt = state.lastTime < 0.0 ? 0.f : static_cast<float>(std::min(time - state.lastTime, 0.1));
        state.lastTime = time;

        std::unordered_set<UUID> alive;
        auto view = world.getAllEntitiesWith<IdComponent, ParticleEmitterComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            const UUID id = view.get<IdComponent>(entityHandle).id;
            const ParticleEmitterComponent &emitter = view.get<ParticleEmitterComponent>(entityHandle);
            const glm::mat4 model = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            EmitterState &emitterState = state.emitters[id];
            alive.insert(id);
            simulate(emitterState, emitter, model, dt);
            submitEmitter(renderer, emitterState, emitter, model, cameraPosition);
        }
        // Emitters that left the world (deleted, component removed) — their state and buffers are dropped.
        for (auto it = state.emitters.begin(); it != state.emitters.end();) {
            it = alive.count(it->first) ? std::next(it) : state.emitters.erase(it);
        }
    }

} // namespace

WorldSystem worldSystem() {
    WorldSystem system;
    system.name = "particles";
    system.submit = submit;
    return system;
}

} // namespace Panda::Particles
