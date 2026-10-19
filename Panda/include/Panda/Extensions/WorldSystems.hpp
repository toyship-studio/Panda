#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Panda {

class World;
class Entity;
class Renderer3D;
struct Occluder;

// Phases of a simulating frame (World::update with simulate), in this order. A system takes part
// in the phases it has hooks for; World runs the hooks of a phase in the registry order
// (WorldSystemRegistry::ordered) and removes the entities destroyed by a hook before the next one.
//   PreFixed     once a frame before the fixed steps; time is the game time of the frame
//   Fixed        once per physics step; time is the step length; the transforms hold the exact
//                body poses (the first step of a frame restores them, World::isFirstFixedStep)
//   PostPhysics  once a frame after the steps; the bodies are at the poses interpolated for the
//                frame (World::fixedStepAlpha)
//   Update       once a frame: gameplay
//   PostUpdate   once a frame after Update: cameras, followers
// The render hooks of a system (submit, occluders) are not phases: they run per render pass.
enum class FramePhase : uint8_t { PreFixed, Fixed, PostPhysics, Update, PostUpdate };

using FrameHook = void (*)(World &, double time);

// A world system as a contribution to the engine registry (context/plugins.md): frame and
// lifecycle hooks that the core calls for all contributions without knowing their types. The core
// systems (physics, scripts) are contributions as well, registered first (CoreWorldSystems). The
// system's per-world state is the world registry context (World::context<T>), World knows nothing
// about it.
struct WorldSystem final {
    std::string name;
    // Frame phases; a null hook means the system skips the phase.
    FrameHook preFixed = nullptr;
    FrameHook fixed = nullptr;
    FrameHook postPhysics = nullptr;
    FrameHook update = nullptr;
    FrameHook postUpdate = nullptr;
    // Order inside every phase: this system runs after / before the named ones. The name "*"
    // stands for every other system, except the ones named in the opposite list and the ones
    // that ask for the opposite themselves. Without constraints the registration order holds; a
    // cycle is reported and the registration order is kept for the systems in it.
    std::vector<std::string> after;
    std::vector<std::string> before;
    // Scene pass: geometry submission after the sky and sprites, before meshes — in all render
    // modes (scene, shadow depth passes, IDS); the mode is held by the renderer.
    void (*submit)(World &, Renderer3D &, const glm::vec3 &cameraPosition, const glm::mat4 &viewProj) = nullptr;
    // Occlusion volume: occluders in the world AABB and the source version (a change — a rebake).
    void (*collectOccluders)(
        World &, const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out
    ) = nullptr;
    uint64_t (*occluderVersion)(World &) = nullptr;
    // The life cycle of a running world, for the systems that keep runtime state per entity
    // (bodies, characters, the colliders of a terrain). start — the world begins to run
    // (World::startRunning: the entities exist, the scripts have not started), in the phase order
    // of the registry (a system that needs another one's state names it in `after`); stop — the
    // world stops (after the scripts' shutdown and the pending destroys), in the reverse order.
    // A system keeps its state in World::context<T> and removes it at stop.
    void (*start)(World &) = nullptr;
    void (*stop)(World &) = nullptr;
    // While the world runs: an entity leaves it (destroyed, or deleted in the editor — the
    // tombstone) or appears with all its components at once (the deletion is undone, or the
    // entity is a duplicate made in the editor). A system owns what it created for the entity:
    // it releases it on removal and creates it again on restore.
    void (*entityRemoved)(World &, Entity) = nullptr;
    void (*entityRestored)(World &, Entity) = nullptr;
    // While the world runs: a component was added to the entity or removed from it (typeId —
    // entt::type_hash of the component type).
    void (*componentsChanged)(World &, Entity, entt::id_type typeId) = nullptr;
    // While the world runs: the transform of an entity was set from outside the simulation (a
    // script setter, the gizmo, the inspector, a reparent). Called for that entity and for every
    // entity under it that a system tracks (World::trackEntity): a parent moved carries what is
    // under it. scale — the scale changed too, the shapes are a part of it.
    void (*transformChanged)(World &, Entity, bool scale) = nullptr;
};

class WorldSystemRegistry final {
public:
    void add(WorldSystem system);
    // Registration order (the render hooks run in it).
    const std::vector<WorldSystem> &systems() const {
        return m_systems;
    }
    // The order of the frame phases: the registration order with the constraints applied.
    const std::vector<const WorldSystem *> &ordered() const {
        return m_ordered;
    }
    const WorldSystem *find(const std::string &name) const;

private:
    void sort();

    std::vector<WorldSystem> m_systems;
    std::vector<const WorldSystem *> m_ordered;
};

} // namespace Panda
