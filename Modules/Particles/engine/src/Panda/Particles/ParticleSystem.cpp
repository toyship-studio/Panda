#include "Panda/Particles/ParticleSystem.hpp"
#include "Panda/Particles/ParticleComponents.hpp"
#include "Panda/Particles/ParticlesResources.hpp"

#include <Foundation/Profile.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/InstancedModel.hpp>
#include <Panda/Renderer/Renderer3D.hpp>
#include <Panda/Renderer/RenderingMode.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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
        glm::vec4 tint{1.f};                 // multiplies the colors of the emitter
        glm::vec4 rect{0.f, 0.f, 1.f, 1.f}; // the part of the texture it shows: x, y, width, height
        glm::quat rotation{1.f, 0.f, 0.f, 0.f}; // a prefab particle with a turn of its own: at birth,
        glm::vec3 spinAxis{0.f, 1.f, 0.f};      // and the axis it spins around
        float phase = 0.f;                       // of its own wandering (turbulence)
    };

    // A stream of instance matrices on the GPU: of the particles of an emitter at one place of
    // its prefab (InstancedModel::places).
    struct MatrixStream {
        Miren::VertexBufferHandle buffer{MIREN_INVALID_HANDLE};
        uint32_t capacity = 0;
    };

    // Live particles of one emitter and its GPU instance buffers.
    struct EmitterState {
        std::vector<Particle> particles;
        float emitAccumulator = 0.f;
        uint32_t seed = 1;
        Miren::VertexBufferHandle instanceBuffer{MIREN_INVALID_HANDLE};
        uint32_t instanceCapacity = 0;
        // An emitter of a prefab: the meshes of the prefab, and the particles as their instances —
        // filled once a frame, drawn by the scene pass and by every shadow pass of the frame.
        InstancedModel model;
        std::vector<MatrixStream> streams; // by place of the model
        uint32_t modelInstances = 0;

        EmitterState() = default;
        EmitterState(const EmitterState &) = delete;
        EmitterState &operator=(const EmitterState &) = delete;
        ~EmitterState() {
            if (instanceBuffer.isValid()) { Miren::deleteVertexBuffer(instanceBuffer); }
            releaseStreams();
        }
        void releaseStreams() {
            for (MatrixStream &stream : streams) {
                if (stream.buffer.isValid()) { Miren::deleteVertexBuffer(stream.buffer); }
            }
            streams.clear();
            modelInstances = 0;
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
        glm::vec4 rect; // of the texture: x, y, width, height
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

    // A particle of the emitter leaves `origin` within the cone around `axis`.
    void spawn(
        EmitterState &state, const ParticleEmitterComponent &emitter, const glm::vec3 &origin, const glm::vec3 &axis,
        const glm::vec4 &tint, const glm::vec4 &rect
    ) {
        Particle particle;
        particle.position = origin;
        if (emitter.emitRadius > 0.f) {
            // Anywhere within the radius, evenly over the volume.
            const glm::vec3 direction = randomDirection(state.seed, glm::vec3(0.f, 1.f, 0.f), glm::pi<float>());
            particle.position += direction * (emitter.emitRadius * std::cbrt(random01(state.seed)));
        }
        if (emitter.prefab.getId() != 0 && emitter.orientation == ParticleOrientation::Random) {
            particle.spinAxis = randomDirection(state.seed, glm::vec3(0.f, 1.f, 0.f), glm::pi<float>());
            particle.rotation = glm::angleAxis(
                random01(state.seed) * glm::two_pi<float>(), randomDirection(state.seed, glm::vec3(0.f, 1.f, 0.f), glm::pi<float>())
            );
        }
        particle.phase = random01(state.seed) * 100.f;
        const float speed = emitter.speed * (1.f + emitter.speedVariation * (random01(state.seed) * 2.f - 1.f));
        particle.velocity = randomDirection(state.seed, axis, glm::radians(emitter.spreadDegrees)) * speed;
        particle.lifetime =
            std::max(0.05f, emitter.lifetime * (1.f + emitter.lifetimeVariation * (random01(state.seed) * 2.f - 1.f)));
        particle.tint = tint;
        particle.rect = rect;
        state.particles.push_back(particle);
    }

    // The up axis of the emitter in the space its particles live in.
    glm::vec3 emitterAxis(const ParticleEmitterComponent &emitter, const glm::mat4 &model) {
        const glm::vec3 axis = emitter.worldSpace ? glm::vec3(model * glm::vec4(0.f, 1.f, 0.f, 0.f)) : glm::vec3(0.f, 1.f, 0.f);
        return glm::length(axis) > 1.e-6f ? axis : glm::vec3(0.f, 1.f, 0.f);
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
            if (emitter.turbulence > 0.f) {
                // A smooth push that keeps turning, each particle on its own phase: three waves
                // of different lengths per axis never repeat together within a lifetime.
                const float t = particle.age + particle.phase;
                const glm::vec3 push(
                    std::sin(t * 1.3f) + std::sin(t * 2.9f + 1.7f), std::sin(t * 1.7f + 4.1f) + std::sin(t * 3.7f + 0.3f),
                    std::sin(t * 1.1f + 2.3f) + std::sin(t * 3.1f + 5.2f)
                );
                particle.velocity += push * (0.5f * emitter.turbulence * dt);
            }
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
        const glm::vec3 axis = emitterAxis(emitter, model);
        while (state.emitAccumulator >= 1.f && particles.size() < maxParticles) {
            state.emitAccumulator -= 1.f;
            spawn(state, emitter, origin, axis, glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));
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
            instance.color = glm::mix(glm::vec4(a.r, a.g, a.b, a.a), glm::vec4(b.r, b.g, b.b, b.a), t) * particle.tint;
            instance.rect = particle.rect;
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

    // The particles of an emitter of a prefab as instance matrices of its meshes: where a
    // particle is × how it is turned × its size, then × the place of a mesh in the prefab.
    void fillModelStreams(EmitterState &state, const ParticleEmitterComponent &emitter, const glm::mat4 &model) {
        if (state.model.stamp() != InstancedModel::stampOfPrefab(emitter.prefab)) {
            state.model = InstancedModel::ofPrefab(emitter.prefab);
            state.releaseStreams();
        }
        state.modelInstances = 0;
        const std::vector<glm::mat4> &places = state.model.places();
        if (state.particles.empty() || places.empty()) { return; }
        // The turn of the emitter, without its scale.
        glm::mat3 emitterAxes(model);
        for (int i = 0; i < 3; i++) {
            const float length = glm::length(emitterAxes[i]);
            emitterAxes[i] = length > 1.e-6f ? emitterAxes[i] / length : glm::mat3(1.f)[i];
        }
        const glm::vec3 worldUp(0.f, 1.f, 0.f);
        std::vector<glm::mat4> poses;
        poses.reserve(state.particles.size());
        for (const Particle &particle : state.particles) {
            const float t = glm::clamp(particle.age / particle.lifetime, 0.f, 1.f);
            const float size = glm::mix(emitter.startSize, emitter.endSize, t);
            const glm::vec3 worldPos =
                emitter.worldSpace ? particle.position : glm::vec3(model * glm::vec4(particle.position, 1.f));
            glm::mat3 axes = emitterAxes;
            if (emitter.orientation == ParticleOrientation::Velocity) {
                const glm::vec3 velocity = emitter.worldSpace ? particle.velocity : glm::mat3(model) * particle.velocity;
                if (glm::length(velocity) > 1.e-5f) {
                    const glm::vec3 forward = glm::normalize(velocity);
                    const glm::vec3 helper = std::abs(forward.y) < 0.99f ? worldUp : glm::vec3(1.f, 0.f, 0.f);
                    const glm::vec3 right = glm::normalize(glm::cross(helper, forward));
                    axes = glm::mat3(right, glm::cross(forward, right), forward);
                }
            } else if (emitter.orientation == ParticleOrientation::Random) {
                const glm::quat spin = glm::angleAxis(glm::radians(emitter.spinDegrees) * particle.age, particle.spinAxis);
                axes = glm::mat3_cast(spin * particle.rotation);
            }
            glm::mat4 pose(axes * size);
            pose[3] = glm::vec4(worldPos, 1.f);
            poses.push_back(pose);
        }
        const uint32_t count = static_cast<uint32_t>(poses.size());
        const uint32_t bytes = count * static_cast<uint32_t>(sizeof(glm::mat4));
        state.streams.resize(places.size());
        std::vector<glm::mat4> matrices(count);
        for (size_t place = 0; place < places.size(); place++) {
            for (uint32_t i = 0; i < count; i++) { matrices[i] = poses[i] * places[place]; }
            MatrixStream &stream = state.streams[place];
            if (!stream.buffer.isValid() || stream.capacity < count) {
                if (stream.buffer.isValid()) { Miren::deleteVertexBuffer(stream.buffer); }
                stream.capacity = std::max(count, std::max(64u, emitter.maxParticles));
                stream.buffer = Miren::createDynamicVertexBuffer(
                    Foundation::Memory::alloc(stream.capacity * sizeof(glm::mat4)),
                    stream.capacity * static_cast<uint32_t>(sizeof(glm::mat4)), StaticResources::instanceMat4Layout
                );
            }
            Miren::updateDynamicVertexBuffer(stream.buffer, Foundation::Memory::copying(matrices.data(), bytes), bytes);
        }
        state.modelInstances = count;
    }

    // A frame of the particles: every emitter moves once — in the scene pass, the first
    // traversal of a frame — and the emitters of prefabs fill their streams there.
    void advance(World &world) {
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
            if (emitter.prefab.getId() != 0) {
                fillModelStreams(emitterState, emitter, model);
            } else if (!emitterState.streams.empty()) {
                emitterState.releaseStreams();
                emitterState.model = InstancedModel();
            }
        }
        // Emitters that left the world (deleted, component removed) — their state and buffers are dropped.
        for (auto it = state.emitters.begin(); it != state.emitters.end();) {
            it = alive.count(it->first) ? std::next(it) : state.emitters.erase(it);
        }
    }

    // The particles that are prefabs: meshes of the world like any other — lit, in the depth
    // of the scene, casting shadows.
    void submit(World &world, Renderer3D &renderer, const glm::vec3 &, const glm::mat4 &) {
        const RenderingMode mode = renderer.getMode();
        if (mode != RenderingMode::DEFAULT && mode != RenderingMode::SHADOW_DEPTH) { return; } // not picked
        PANDA_PROFILE_SCOPE_N("World: particles");
        if (mode == RenderingMode::DEFAULT) { advance(world); }
        ParticlesState &state = world.context<ParticlesState>();
        glm::mat4 identity(1.f); // the instances are already in world space
        for (auto &[id, emitterState] : state.emitters) {
            if (emitterState.modelInstances == 0) { continue; }
            for (size_t place = 0; place < emitterState.streams.size(); place++) {
                emitterState.model.submit(
                    renderer, identity, static_cast<uint32_t>(place), emitterState.streams[place].buffer, emitterState.modelInstances
                );
            }
        }
    }

    // The particles that are quads: blended over the finished scene.
    void submitBlended(World &world, Renderer3D &renderer, const glm::vec3 &cameraPosition, const glm::mat4 &) {
        if (renderer.getMode() != RenderingMode::DEFAULT) { return; } // neither shadows nor picking
        PANDA_PROFILE_SCOPE_N("World: particle quads");
        ParticlesState &state = world.context<ParticlesState>();
        auto view = world.getAllEntitiesWith<IdComponent, ParticleEmitterComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            const ParticleEmitterComponent &emitter = view.get<ParticleEmitterComponent>(entityHandle);
            if (emitter.prefab.getId() != 0) { continue; }
            const auto found = state.emitters.find(view.get<IdComponent>(entityHandle).id);
            if (found == state.emitters.end()) { continue; }
            const glm::mat4 model = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            submitEmitter(renderer, found->second, emitter, model, cameraPosition);
        }
    }

} // namespace

uint32_t emit(
    World &world, Entity entity, uint32_t count, const glm::vec3 *position, const glm::vec4 &tint, const glm::vec4 &rect
) {
    if (!entity.isValid() || !entity.hasComponent<ParticleEmitterComponent>()) { return 0; }
    const ParticleEmitterComponent &emitter = entity.getComponent<ParticleEmitterComponent>();
    const glm::mat4 model = world.getWorldSpaceTransformMatrix(entity);
    // The particles of a local emitter live in its space: a place of the world is brought there.
    glm::vec3 origin = emitter.worldSpace ? glm::vec3(model[3]) : glm::vec3(0.f);
    if (position != nullptr) {
        origin = emitter.worldSpace ? *position : glm::vec3(glm::inverse(model) * glm::vec4(*position, 1.f));
    }
    EmitterState &state = world.context<ParticlesState>().emitters[entity.getId()];
    const glm::vec3 axis = emitterAxis(emitter, model);
    const uint32_t maxParticles = std::max(1u, emitter.maxParticles);
    uint32_t emitted = 0;
    while (emitted < count && state.particles.size() < maxParticles) {
        spawn(state, emitter, origin, axis, tint, rect);
        emitted++;
    }
    return emitted;
}

WorldSystem worldSystem() {
    WorldSystem system;
    system.name = "particles";
    // The particles that are prefabs are meshes of the world; the quads are blended and go
    // after the opaque world.
    system.submit = submit;
    system.submitBlended = submitBlended;
    return system;
}

} // namespace Panda::Particles
