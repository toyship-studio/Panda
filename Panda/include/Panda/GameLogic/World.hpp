//
// Created by Admin on 25.01.2022.
//

#pragma once

#include "Panda/Base/TypeContext.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/WorldSettings.hpp"
#include "Panda/GameLogic/Systems/LightingSystem.hpp"
#include "Panda/Renderer/Renderer2D.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Renderer/SceneWind.hpp"
#include "Panda/WorldCommands/CommandHistory.hpp"
#include "Panda/Extensions/WorldSystems.hpp"

#include <Miren/Miren.hpp>

#include <cstdint>
#include <vector>

namespace Panda {

// World frame: what to draw with and what to do. The camera is either explicit matrices (editor:
// the viewport camera) or the world's main camera (runtime; resolved AFTER the simulation — scripts
// move the camera within the same frame). update stages: simulation (the frame phases of the world
// systems, FramePhase: physics and scripts among them) → scene pass → shadow cascades. Additional
// passes (picking, editor overlays) — renderPass.
struct WorldFrame {
    bool mainCamera = false;
    glm::mat4 viewProj = glm::mat4(1.f);
    glm::mat4 skyViewProj = glm::mat4(1.f);
    glm::vec3 cameraPosition = glm::vec3(0.f);
    bool simulate = false; // physics and scripts (Play)
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
    void startRunning();
    void finishRunning();
    // World frame by stages (WorldFrame). The runtime calls it only on a running world.
    void update(double deltaTime, WorldFrame frame);
    // Additional world pass in a rendering mode into a view (editor: IDS picking) — after
    // update of the same frame, with the same frame.
    void renderPass(RenderingMode mode, Miren::ViewId view, const WorldFrame &frame);
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
    // Entity by registry handle (the handle comes from a view of this world: getAllEntitiesWith).
    Entity entity(entt::entity handle) {
        return Entity(handle, this);
    }
    inline bool isRunning() {
        return m_isRunning;
    }
    Renderer2D &getRenderer2D() {
        return m_renderer2d;
    }
    Renderer3D &getRenderer3D() {
        return m_renderer3d;
    }
    // The core settings of the world: sections of the world scope of the settings registry
    // (Extensions::settings — "lighting", "graphics", "wind" in .world) in the context, like the
    // settings of the modules. They exist from the world's birth; clear resets them.
    WorldGraphicsSettings &getGraphicsSettings() {
        return context<WorldGraphicsSettings>();
    }
    const WorldGraphicsSettings &getGraphicsSettings() const {
        return settings<WorldGraphicsSettings>();
    }
    WorldLightingSettings &getLightingSettings() {
        return context<WorldLightingSettings>();
    }
    const WorldLightingSettings &getLightingSettings() const {
        return settings<WorldLightingSettings>();
    }
    // Scene wind of the world. It is applied to the global SceneWind by the application LAYER
    // (EditorLayer/RuntimeLayer): at startup — before the UI is built (pinned tabs see the
    // world's value) — and every frame.
    SceneWindSettings &getWindSettings() {
        return context<SceneWindSettings>();
    }
    const SceneWindSettings &getWindSettings() const {
        return settings<SceneWindSettings>();
    }
    // Screen position of the frame's sun (xy — UV, z — "in frame" 0/1): computed from the
    // CAMERA's viewProj in update*, consumed by postprocessing (flares in bloom_apply).
    glm::vec4 getFrameSunScreen() const {
        return m_lighting.sunScreen();
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
    // Entity order for the Hierarchy: those with children first, then by name; children — the same.
    void sort();
    Entity duplicateEntity(Entity entity);
    bool isValidEntt(entt::entity handle);
    bool isValid(UUID entityId);

private:
    // World pass in the current rendering mode: frame lighting (DEFAULT only), then feeding
    // geometry by families (RenderSystem). In the shadow depth pass viewProjMtx is the cascade's.
    void renderWorld(glm::mat4 &viewProjMtx, glm::mat4 &skyViewProjMtx, glm::vec3 cameraPosition);
    // Depth passes of the shadow cascades (after the DEFAULT traversal of the same frame: it has
    // computed the cascades for the camera and the sun; the GPU executes them before the scene —
    // the views are above SCENE_VIEW).
    void renderShadowCascades(glm::vec3 cameraPosition);
    // Matrices and position of the world's main camera into the frame; without a camera —
    // identity matrices.
    void resolveMainCamera(WorldFrame &frame);
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
    Entity duplicateSubtree(Entity entity);
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
    uint64_t m_structureVersion = 0;
    CommandHistory m_commandHistory;
    entt::registry m_registry;
    TypeContext m_context;
    Renderer2D m_renderer2d;
    Renderer3D m_renderer3d;
    LightingSystem m_lighting; // frame lighting: lights, shadow cascades, occlusion volume

    friend class Entity;
};

} // namespace Panda
