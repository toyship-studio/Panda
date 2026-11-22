#pragma once

#include "Panda/Base/TypeContext.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/WorldSettings.hpp"
#include "Panda/Renderer/SceneWind.hpp"
#include "Panda/WorldCommands/CommandHistory.hpp"
#include "Panda/Extensions/WorldSystems.hpp"

#include <Miren/Miren.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Panda {

class PrefabAsset;

// Where a copy of entities stands (World::duplicateEntity, World::instantiate): under which
// entity, and the position and the rotation of its root in the space of that parent. What is
// not said stays as the original has it.
struct EntityPlacement final {
    Entity parent; // invalid — a duplicate stands next to its original, an instance is a root of the world
    std::optional<glm::vec3> position;
    std::optional<glm::quat> rotation;
};

class World final {
public:
    World();
    // Pools of all registered component types (the world copy and the entity duplicate walk the
    // pools without types). Called on creation; again — after plugins are loaded into a live
    // world (the project's plugins when it is opened).
    void prepareComponentTypes();
    World(World &world) = delete;
    ~World();
    // A world is a self-contained object: its entities, settings, the state of its systems, its
    // renderers and its undo history are its own, and any number of worlds may be alive (the
    // one being edited, a playing copy, a scratch world of a clipboard fragment). Running is
    // exclusive: a process is one game, so one world runs at a time, and between startRunning
    // and finishRunning it is the world the scripts act in (GameContext::getRunningWorld).
    void startRunning();
    void finishRunning();
    // A frame of the simulation: the frame phases of the world systems (FramePhase), physics and
    // scripts among them. Called on a running world only. Drawing is not here: a world is drawn
    // through a view (Renderer/WorldView.hpp), after its update — the scripts move the camera in it.
    void update(double deltaTime);
    Entity instantiateEntity();
    // Entity with a known id (loading the world from a file — WorldMapper).
    Entity instantiateEntity(UUID id);
    // Script classes and fields — by the manifest of the loaded DLL (after load/reload).
    void updateScriptsAndFields();
    // Removes the entity and its children from the world. A world that does not run does it at
    // once. A running world does it between the callback passes of its frame: a script may
    // destroy any entity, its own included, from any callback, and no pass over the scripts and
    // no contact dispatch walks a dead entity. Until then the entity stays in the world as it
    // was.
    void destroy(Entity entity);
    // The only way to change the hierarchy: puts the entity under a parent, or makes it a root
    // (an invalid parent). Everything that depends on the link is kept together here — both of
    // its sides, the transform, the count of bodies above, the structure version and the change
    // flag. With keepWorldTransform the entity stays where it is in the world: its transform is
    // recomputed for the new parent. Without it the transform is taken as it is, relative to
    // the new parent, and the bodies under the entity are rebuilt at their new place. A parent
    // that is the entity itself or stands under it is refused.
    void setParent(Entity entity, Entity parent, bool keepWorldTransform = true);
    void clear();
    bool isEmpty();
    Entity findByTag(const char *tag);
    std::vector<Entity> findAllByTag(const char *tag);
    Entity getById(UUID id);
    std::unordered_set<Entity> getById(std::unordered_set<UUID> ids);
    // The entity and everything under it: a parent before its children, the children in their
    // order. A deleted entity is not in it (nor is what stands under it), and neither is one
    // that `skip` says yes to. An invalid entity — nothing.
    std::vector<Entity> subtree(Entity entity, const std::function<bool(Entity)> &skip = {});
    // Entity by registry handle (the handle comes from a view of this world: getAllEntitiesWith).
    Entity entity(entt::entity handle) {
        return Entity(handle, this);
    }
    inline bool isRunning() {
        return m_isRunning;
    }
    // The core settings of the world, as fields (WorldSettings: "lighting", "graphics", "wind"
    // in .world), in the context like the settings of the modules. They exist from the world's
    // birth; clear resets them.
    WorldSettings &getSettings() {
        return context<WorldSettings>();
    }
    const WorldSettings &getSettings() const {
        return settings<WorldSettings>();
    }
    glm::mat4 getWorldSpaceTransformMatrix(Entity entity);
    void convertToWorldSpace(Entity entity);
    void convertToLocalSpace(Entity entity);
    // Puts the entity at a position and a rotation given in world space: they go into its
    // transform relative to the parent, where the parent is now. The scale stays as it is.
    void setWorldPose(Entity entity, glm::vec3 position, glm::quat rotation);
    Entity findMainCameraEntity();
    Camera *findMainCamera();

    template<typename... Components>
    auto getAllEntitiesWith() {
        return m_registry.view<Components...>();
    }

    // The context of the world (TypeContext): the state of the world systems and the settings
    // of the world scope, by type. World does not know the types of the systems; their lifetime
    // and cleanup are the system's responsibility (eraseContext in its own stop hook). The
    // settings are the registry's: created at birth, reset by clear, copied with the world.
    template<typename T, typename... Args>
    T &context(Args &&...args) {
        return m_context.get<T>(std::forward<Args>(args)...);
    }
    template<typename T>
    T *findContext() {
        return m_context.find<T>();
    }
    template<typename T>
    void eraseContext() {
        m_context.erase<T>();
    }
    TypeContext &context() {
        return m_context;
    }
    const TypeContext &context() const {
        return m_context;
    }

    World &operator=(World &other);

    // Inside the Fixed phase: the first step of the frame (the body poses are restored to the exact
    // ones before it).
    bool isFirstFixedStep() const {
        return m_firstFixedStep;
    }
    // After the fixed steps of the frame: where the frame falls between two steps, 0..1.
    float fixedStepAlpha() const {
        return m_fixedStepAlpha;
    }
    // A system that keeps runtime state on an entity (a body) tells the world so that a transform
    // change above the entity reaches it (WorldSystem::transformChanged): the world counts the
    // tracked entities by subtree and walks only the branches that have some. Every track is
    // untracked when the state goes (the world asserts it empty when it stops).
    void trackEntity(Entity entity);
    void untrackEntity(Entity entity);

    // Command history of the world (editor undo): each instance has its own — edits in Play
    // accumulate in the playing world and go away with it; the runtime does not fill it.
    CommandHistory &getCommandHistory() {
        return m_commandHistory;
    }
    // Has unsaved changes (set by all content edits; reset by load/save).
    bool isChanged() const {
        return m_changed;
    }
    void setChanged(bool changed = true) {
        m_changed = changed;
        if (changed) { m_revision++; }
    }
    // Grows with every change of the content (each setChanged): a copy of the world made at one
    // revision is behind the world at another (the backups of the editor).
    uint64_t getRevision() const {
        return m_revision;
    }
    // World structure version: grows on entity creation/deletion/reparenting (including from
    // scripts in PLAY) — the editor rereads the Hierarchy and cleans the selection when it changes.
    uint64_t getStructureVersion() const {
        return m_structureVersion;
    }
    void bumpStructureVersion() {
        ++m_structureVersion;
    }
    // Tombstones (DeletedComponent): an entity deleted by a command lives in the registry for undo.
    bool needToDestroy(Entity entity);
    bool isDeleted(entt::entity handle); // itself or any ancestor
    // The order of siblings — of the entities without a parent (parent 0), or of the children of
    // an entity: the ones with children first, then by name, then by id (two entities of one
    // name keep their places). It is the order of the Hierarchy and of the world file. Deleted
    // entities are left out. The order is derived when asked: nothing stores it, so nothing has
    // to keep it up when an entity is added, renamed or moved.
    std::vector<UUID> orderedChildren(UUID parent);
    // The links of the hierarchy against the entities that exist, after a load: a child id without
    // an entity or whose parent is another entity leaves the list, a child the parent does not
    // list is added, a parent id without an entity and a cycle make the entity a root. The parent
    // field is the truth. Returns the number of repaired links; each is logged.
    size_t repairHierarchy();
    // A copy of the entity with everything under it, next to it. A copy — this one, an import, an
    // instance — points at its own entities: a reference between two copied entities
    // (ComponentType::entityRefs: a field of a script, a joint) names the copies, a reference to
    // an entity outside the copied ones keeps its id. In a running world the copy lives at once:
    // the systems get its entities, its scripts are created and started.
    Entity duplicateEntity(Entity entity, const EntityPlacement &placement = {});
    // A copy of an entity of ANOTHER world with everything under it, with new ids: a root of this
    // world, or under the parent of the placement with its transform as it was under its own.
    // What a paste is made of, and what Apply puts into a prefab. `ids` — the id of every copy by
    // the id of its original; `skip` — an entity under it that is not copied, with everything
    // under it.
    Entity importEntity(
        World &source, Entity entity, const EntityPlacement &placement = {}, std::unordered_map<UUID, UUID> *ids = nullptr,
        const std::function<bool(Entity)> &skip = {}
    );
    // A new instance of a prefab as a root of this world: copies of the entities of the prefab,
    // each marked with where it came from (PrefabComponent). `rootId` — the id of the root of the
    // instance, 0 for a new one; the ids of the entities under it are derived from it
    // (derivedEntityId), so whoever chooses the root id knows them all. Invalid — the prefab has
    // no entities (its file is missing or empty).
    Entity instantiate(const AssetRef<PrefabAsset> &prefab, UUID rootId = UUID(0), const EntityPlacement &placement = {});
    // The entity gets another id, and everything of the world that names it names it by the new
    // one: its parent and its children, the references of the components
    // (ComponentType::entityRefs). false — the id is taken, nothing changed. For a world that
    // does not run.
    bool changeEntityId(Entity entity, UUID id);
    // An id no entity of this world has and no asset either (a field of a script that takes an
    // entity takes a prefab too, by the id of its asset). Never 0.
    UUID newEntityId();
    bool isValidEntt(entt::entity handle);
    bool isValid(UUID entityId);

private:
    void fillEntity(Entity entity, UUID id);
    // The two sides of a link and the count of bodies above it, with nothing of what setParent
    // does around them: its halves, and the way an entity leaves the hierarchy when it is
    // destroyed and enters it as a duplicate.
    void attach(Entity entity, Entity parent);
    void detach(Entity entity);
    void destroyNow(Entity entity);
    void destroySubtree(Entity entity);
    // A settings section of the world scope for a const reader: the registry keeps it alive.
    template<typename T>
    const T &settings() const {
        const T *value = m_context.find<T>();
        PND_ASSERT(value != nullptr, "WORLD SETTINGS ARE NOT REGISTERED (Extensions::settings)");
        return *value;
    }
    void destroyPending();
    // One frame phase: the hooks of the world systems in the registry order, destroyPending after
    // each.
    void runPhase(FramePhase phase, double time);
    // The entity of `source` and everything under it, copied into this world; `source` is this
    // world for a duplicate, another one for an import, the world of a prefab for an instance —
    // then `prefab` is its asset and every copy is marked as a part of the instance. `copies`
    // gets the copied entities, the root first.
    Entity copySubtree(
        World &source, Entity entity, AssetId prefab, UUID rootId, std::vector<Entity> &copies,
        std::unordered_map<UUID, UUID> *idsOut = nullptr, const std::function<bool(Entity)> &skip = {}
    );
    // The copy takes its place — under `parent`, at the pose of the placement — and, in a running
    // world, comes to life: the systems hear of its entities, which have just appeared with all
    // their components, and their scripts are created and started where they stand.
    void settle(Entity root, const std::vector<Entity> &copies, Entity parent, const EntityPlacement &placement);
    // The tombstone of the entity was put on or taken off (Entity::setDeleted): the running
    // systems lose or get back the entity and everything under it (entityRemoved / entityRestored).
    void deletionChanged(Entity entity);
    // The transform of the entity was changed from outside the simulation (Entity::transformUpdated,
    // scaleUpdated). Everything under the entity has moved with it: the systems hear about the
    // entity and about every tracked entity under it (WorldSystem::transformChanged). Only the
    // branches with tracked entities are walked (m_subtreeTracked).
    void transformChanged(Entity entity, bool scale);
    // The component set of the entity changed (Entity::addComponent / removeComponent): the
    // running systems hear about it (WorldSystem::componentsChanged).
    void componentsChanged(Entity entity, entt::id_type typeId);
    // The life cycle hooks of the world systems (WorldSystem::start / stop, entityRemoved /
    // entityRestored), in the phase order and in the reverse one.
    void runStartHooks();
    void runStopHooks();
    void runEntityHooks(Entity entity, bool removed);
    // The entityRestored hooks for an entity and everything under it (a duplicate made while
    // the world runs).
    void restoreSubtree(Entity entity);
    // The number of tracked entities on the entity and under it, and a change of it — for the
    // entity and for everything above it.
    int32_t subtreeTracked(UUID entityId) const;
    void shiftSubtreeTracked(Entity entity, int32_t delta);

    std::unordered_map<UUID, Entity> m_entityIdMap;
    // Where the tracked entities of the running systems stand in the hierarchy: the number of
    // them on an entity and under it, for the entities that have any. A walk over a subtree that
    // looks for them skips the branches that are not here. The systems report every one
    // (trackEntity, untrackEntity), and a subtree takes its count along when it changes its
    // parent (attach, detach).
    std::unordered_map<UUID, int32_t> m_subtreeTracked;
    bool m_isRunning;
    double m_physicsAccumulator = 0.0; // frame time not yet spent in physics steps, s
    bool m_firstFixedStep = false;
    float m_fixedStepAlpha = 0.f;
    std::vector<entt::entity> m_pendingDestroy; // destroy in a running world: until the callbacks are over
    bool m_changed = false;
    uint64_t m_revision = 0;
    uint64_t m_structureVersion = 0;
    CommandHistory m_commandHistory;
    entt::registry m_registry;
    TypeContext m_context;

    friend class Entity;
};

} // namespace Panda
