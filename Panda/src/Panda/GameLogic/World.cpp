//
// Created by Admin on 25.01.2022.
//

#include "Panda/GameLogic/World.hpp"
#include "Panda/Serialization/SettingsMapper.hpp"
#include "Panda/GameLogic/Input.hpp"
#include "Panda/GameLogic/Systems/RenderSystem.hpp"
#include "Panda/GameLogic/Systems/ScriptSystem.hpp"
#include <Foundation/Profile.hpp>
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"
#include "Panda/Assets/StaticResources.hpp"

#include <Rain/Rain.hpp>
#include <entt/entt.hpp>
#include <algorithm>

namespace Panda {

namespace {
    // Physics runs with a fixed step: the frame time is accumulated and spent in whole steps, the
    // remainder waits for the next frame. 60 steps per second with 4 substeps each: the solver
    // works as hard as it did with one step per frame at 60 frames per second.
    constexpr double PHYSICS_STEP = 1.0 / 60.0;
    // The game time of a frame is its real time, but no more than this many steps. A longer
    // frame (a slow one, a hitch, a breakpoint) counts as this much for the physics and for the
    // frame callbacks alike: below 15 frames per second the whole game slows down instead of
    // catching up with a burst of steps, and a timer of a script never runs ahead of the physics.
    constexpr int MAX_FRAME_STEPS = 4;
    constexpr double MAX_FRAME_TIME = PHYSICS_STEP * MAX_FRAME_STEPS;

    // The rotation of a matrix that also scales, the way a transform reads it from a matrix
    // (glm::decompose): the axes are made orthonormal in the order X, Y, Z, and a mirror is left
    // to the scale.
    glm::quat rotationOf(const glm::mat3 &matrix) {
        glm::vec3 x = glm::normalize(matrix[0]);
        glm::vec3 y = glm::normalize(matrix[1] - x * glm::dot(x, matrix[1]));
        if (glm::determinant(matrix) < 0.f) {
            x = -x;
            y = -y;
        }
        return glm::quat_cast(glm::mat3(x, y, glm::cross(x, y)));
    }
} // namespace

World::World()
    : m_entityIdMap(100)
    , m_isRunning(false)
    , m_registry() {
    // Pools of all registered types: the world copy and the entity duplicate walk the pools
    // without types, the receiver must have them.
    prepareComponentTypes();
    // The settings of the world scope exist from birth, with their defaults.
    SettingsMapper::reset(SettingsScope::World, m_context);
}

void World::prepareComponentTypes() {
    GameContext::getExtensions().components.prepare(m_registry);
}

World::~World() {
    // A world destroyed while running (the layer goes down in Play): the systems release their
    // state before the context goes.
    if (m_isRunning) {
        runStopHooks();
        m_isRunning = false;
    }
}

void World::startRunning() {
    m_isRunning = true; // from here on destroy waits for the callbacks to end: start is one of them
    m_physicsAccumulator = 0.0;
    Input::skipFixedTick(); // a key pressed before the start is not a press of the first step
    runStartHooks();
    ScriptSystem::instantiate(*this);
    ScriptSystem::start(*this);
    destroyPending();
}

void World::finishRunning() {
    ScriptSystem::shutdown(*this);
    // What was asked to be destroyed is destroyed before the world stops — the requests of the
    // last frame and of the scripts' shutdown alike: the world may be started again as it is.
    while (!m_pendingDestroy.empty()) { destroyPending(); }
    runStopHooks();
    PND_ASSERT(m_subtreeTracked.empty(), "ENTITIES ARE STILL TRACKED AFTER THE SYSTEMS HAVE STOPPED");
    m_isRunning = false;
}

void World::runStartHooks() {
    for (const WorldSystem *system : GameContext::getExtensions().worldSystems.ordered()) {
        if (system->start != nullptr) { system->start(*this); }
    }
}

void World::runStopHooks() {
    const std::vector<const WorldSystem *> systems = GameContext::getExtensions().worldSystems.ordered();
    for (auto it = systems.rbegin(); it != systems.rend(); ++it) {
        if ((*it)->stop != nullptr) { (*it)->stop(*this); }
    }
}

void World::runEntityHooks(Entity entity, bool removed) {
    for (const WorldSystem *system : GameContext::getExtensions().worldSystems.ordered()) {
        const auto hook = removed ? system->entityRemoved : system->entityRestored;
        if (hook != nullptr) { hook(*this, entity); }
    }
}

void World::componentsChanged(Entity entity, entt::id_type typeId) {
    if (!m_isRunning) { return; }
    for (const WorldSystem *system : GameContext::getExtensions().worldSystems.ordered()) {
        if (system->componentsChanged != nullptr) { system->componentsChanged(*this, entity, typeId); }
    }
}

void World::updateScriptsAndFields() {
    ScriptSystem::syncWithManifest(*this);
}

void World::runPhase(FramePhase phase, double time) {
    for (const WorldSystem *system : GameContext::getExtensions().worldSystems.ordered()) {
        FrameHook hook = nullptr;
        switch (phase) {
            case FramePhase::PreFixed: hook = system->preFixed; break;
            case FramePhase::Fixed: hook = system->fixed; break;
            case FramePhase::PostPhysics: hook = system->postPhysics; break;
            case FramePhase::Update: hook = system->update; break;
            case FramePhase::PostUpdate: hook = system->postUpdate; break;
        }
        if (hook == nullptr) { continue; }
        hook(*this, time);
        destroyPending(); // what the hook asked to destroy is gone before the next hook
    }
}

void World::update(double deltaTime, WorldFrame frame) {
    m_renderer3d.resetStats();
    m_renderer2d.begin(RenderingMode::DEFAULT, Views::SCENE_VIEW);
    m_renderer3d.begin(RenderingMode::DEFAULT, Views::SCENE_VIEW);
    if (frame.simulate) {
        const double gameDelta = std::min(deltaTime, MAX_FRAME_TIME);
        destroyPending(); // asked for between the frames (a UI event of a script)
        // The frame phases (FramePhase); physics and scripts are world systems like the modules
        // (CoreWorldSystems). Every physics step is one Fixed phase: physics.begin, the scripts'
        // fixedUpdate, physics.step. Between the steps the transforms hold the exact body poses;
        // the first step of the frame restores them after the interpolated write of the last
        // frame (isFirstFixedStep).
        runPhase(FramePhase::PreFixed, gameDelta);
        m_physicsAccumulator += gameDelta;
        m_firstFixedStep = true;
        while (m_physicsAccumulator >= PHYSICS_STEP) {
            m_physicsAccumulator -= PHYSICS_STEP;
            Input::beginFixedTick();
            runPhase(FramePhase::Fixed, PHYSICS_STEP);
            Input::endFixedTick();
            m_firstFixedStep = false;
        }
        m_firstFixedStep = false;
        // The frame falls between two steps: the bodies are drawn in between, one step behind.
        m_fixedStepAlpha = static_cast<float>(m_physicsAccumulator / PHYSICS_STEP);
        runPhase(FramePhase::PostPhysics, gameDelta);
        runPhase(FramePhase::Update, gameDelta);
        runPhase(FramePhase::PostUpdate, gameDelta);
    }
    if (frame.mainCamera) { resolveMainCamera(frame); }
    m_renderer2d.setViewProj(frame.viewProj);
    m_renderer3d.setViewProj(frame.viewProj);
    renderWorld(frame.viewProj, frame.skyViewProj, frame.cameraPosition);
    m_renderer2d.end();
    m_renderer3d.end();
    renderShadowCascades(frame.cameraPosition);
}

void World::renderPass(RenderingMode mode, Miren::ViewId view, const WorldFrame &frame) {
    glm::mat4 viewProj = frame.viewProj;
    glm::mat4 skyViewProj = frame.skyViewProj;
    m_renderer2d.begin(mode, view);
    m_renderer3d.begin(mode, view);
    m_renderer2d.setViewProj(viewProj);
    m_renderer3d.setViewProj(viewProj);
    renderWorld(viewProj, skyViewProj, frame.cameraPosition);
    m_renderer2d.end();
    m_renderer3d.end();
}

void World::resolveMainCamera(WorldFrame &frame) {
    Entity cameraEntity = findMainCameraEntity();
    if (!cameraEntity.isValid()) { return; }
    WorldCamera &camera = cameraEntity.getComponent<CameraComponent>().camera;
    const glm::mat4 cameraWorldMtx = getWorldSpaceTransformMatrix(cameraEntity);
    const glm::mat4 projMtx = camera.getProjection();
    frame.viewProj = projMtx * glm::inverse(cameraWorldMtx);
    frame.skyViewProj = projMtx * glm::inverse(cameraEntity.getTransform().getSkyTransform());
    frame.cameraPosition = glm::vec3(cameraWorldMtx * glm::vec4(0.f, 0.f, 0.f, 1.f));
}

void World::renderWorld(glm::mat4 &viewProjMtx, glm::mat4 &skyViewProjMtx, glm::vec3 cameraPosition) {
    PANDA_PROFILE_SCOPE_N("World::renderWorld");
    // Lighting is needed only by the DEFAULT pass (IDS/GEOMETRY_ONLY do not bind it) — do not
    // collect it for nothing in the picking pass.
    if (m_renderer3d.getMode() == RenderingMode::DEFAULT) {
        const WorldLightingData &lighting = m_lighting.update(*this, viewProjMtx, cameraPosition);
        m_renderer3d.setLighting(lighting);
        m_renderer2d.setLighting(lighting);
    }
    const bool shadowPass = m_renderer3d.getMode() == RenderingMode::SHADOW_DEPTH;
    const RenderSystem::Frustum frustum = RenderSystem::Frustum::of(viewProjMtx);
    RenderSystem::submitSky(*this, m_renderer3d, skyViewProjMtx);
    if (!shadowPass) { RenderSystem::submitSprites(*this, m_renderer2d, frustum); }
    // World systems (contributions of the worldSystems registry) — between sprites and meshes, in
    // all modes.
    for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
        if (system.submit != nullptr) { system.submit(*this, m_renderer3d, cameraPosition, viewProjMtx); }
    }
    RenderSystem::submitMeshes(*this, m_renderer3d, frustum, shadowPass);
}

void World::renderShadowCascades(glm::vec3 cameraPosition) {
    PANDA_PROFILE_SCOPE_N("World::renderShadowCascades");
    // World traversal per cascade: culling by the light frustum, the LOD of the world systems —
    // by the camera (the same mesh as in the scene — otherwise the surface shadows itself at the
    // LOD seams).
    CascadedShadowMap &shadowMap = m_lighting.shadowMap();
    for (uint32_t cascade = 0; cascade < shadowMap.cascadeCount(); cascade++) {
        m_renderer3d.begin(RenderingMode::SHADOW_DEPTH, shadowMap.cascadeView(cascade));
        glm::mat4 viewProj = shadowMap.cascadeViewProj(cascade);
        m_renderer3d.setViewProj(viewProj);
        if (cascade == 0) {
            // The atlas is cleared by the view of the first cascade, and a view without draw calls
            // is not executed: touch guarantees the clear — otherwise ghost shadows remain after
            // the casters disappear.
            Miren::touch(shadowMap.cascadeView(cascade));
        }
        renderWorld(viewProj, viewProj, cameraPosition);
        m_renderer3d.end();
    }
}

Entity World::instantiateEntity() {
    entt::entity entityHandle = m_registry.create();
    Entity entity = {entityHandle, this};
    fillEntity(entity, {});
    m_entityIdMap[entity.getId()] = entity;
    bumpStructureVersion();
    m_changed = true;
    return entity;
}

Entity World::instantiateEntity(UUID id) {
    entt::entity entityHandle = m_registry.create();
    Entity entity = {entityHandle, this};
    fillEntity(entity, id);
    bumpStructureVersion();
    m_entityIdMap[entity.getId()] = entity;
    m_changed = true;
    return entity;
}

void World::fillEntity(Entity entity, UUID id) {
    entity.addComponent<IdComponent>(id);
    entity.addComponent<TagComponent>("Entity");
    entity.addComponent<RelationshipComponent>();
    entity.addComponent<TransformComponent>();
    entity.addComponent<ScriptListComponent>();
}

void World::destroy(Entity entity) {
    if (m_isRunning) {
        m_pendingDestroy.push_back(entity.m_handle);
        return;
    }
    destroyNow(entity);
}

void World::setParent(Entity entity, Entity parent, bool keepWorldTransform) {
    const UUID parentId = parent ? parent.getId() : UUID(0);
    if (m_registry.get<RelationshipComponent>(entity.m_handle).parent == parentId) { return; }
    if (parent && (parent == entity || entity.isAncestorOf(parent))) {
        LOG_ERROR_EDITOR("The entity %s can't be moved into one of its children.", entity.getName().c_str());
        return;
    }
    if (keepWorldTransform) { convertToWorldSpace(entity); }
    detach(entity);
    if (parent) {
        attach(entity, parent);
        if (keepWorldTransform) { convertToLocalSpace(entity); }
    }
    // A transform taken as it is puts the subtree elsewhere in the world, maybe at another
    // scale: the bodies under the entity are rebuilt where they stand now.
    if (!keepWorldTransform) { transformChanged(entity, true); }
    bumpStructureVersion();
    m_changed = true;
}

void World::attach(Entity entity, Entity parent) {
    m_registry.get<RelationshipComponent>(entity.m_handle).parent = parent.getId();
    m_registry.get<RelationshipComponent>(parent.m_handle).children.push_back(entity.getId());
    // The tracked entities of the subtree are now under the parent and everything above it.
    shiftSubtreeTracked(parent, subtreeTracked(entity.getId()));
}

void World::detach(Entity entity) {
    RelationshipComponent &relationship = m_registry.get<RelationshipComponent>(entity.m_handle);
    // A parent under a tombstone is still in the registry and still lists the entity.
    const auto parent = m_entityIdMap.find(relationship.parent);
    if (parent != m_entityIdMap.end()) {
        auto &children = m_registry.get<RelationshipComponent>(parent->second.m_handle).children;
        children.erase(std::remove(children.begin(), children.end(), entity.getId()), children.end());
        // The tracked entities of the subtree leave with it.
        shiftSubtreeTracked(parent->second, -subtreeTracked(entity.getId()));
    }
    relationship.parent = 0;
}

void World::destroyNow(Entity entity) {
    detach(entity);
    destroySubtree(entity);
    bumpStructureVersion();
    m_changed = true;
}

void World::destroySubtree(Entity entity) {
    // The children go with it, the ones under a tombstone too: the registry decides who is
    // alive here, not the visibility of the entity. They stay linked: the whole subtree goes.
    const std::vector<UUID> children = entity.getChildEntities();
    for (UUID childId : children) {
        Entity child = getById(childId);
        if (m_registry.valid(child.m_handle)) { destroySubtree(child); }
    }
    const UUID entityId = entity.getId();
    if (ExternalCalls::entityWillBeDeleted) { ExternalCalls::entityWillBeDeleted(entityId); }
    // In a running world the state the systems keep for the entity leaves with it.
    if (m_isRunning) { runEntityHooks(entity, /*removed*/ true); }
    m_entityIdMap.erase(entityId);
    m_registry.destroy(entity.m_handle);
}

void World::deletionChanged(Entity entity) {
    if (!m_isRunning) { return; }
    runEntityHooks(entity, /*removed*/ !entity.isValid());
    // The tombstone covers everything under the entity; a child may have one of its own.
    const std::vector<UUID> children = entity.getChildEntities();
    for (UUID childId : children) {
        Entity child = getById(childId);
        if (m_registry.valid(child.m_handle)) { deletionChanged(child); }
    }
}

void World::transformChanged(Entity entity, bool scale) {
    // Only tracked entities follow a transform; a world that does not run has none.
    if (subtreeTracked(entity.getId()) == 0) { return; }
    for (const WorldSystem *system : GameContext::getExtensions().worldSystems.ordered()) {
        if (system->transformChanged != nullptr) { system->transformChanged(*this, entity, scale); }
    }
    for (UUID childId : entity.getChildEntities()) {
        if (subtreeTracked(childId) != 0) { transformChanged(getById(childId), scale); }
    }
}

void World::trackEntity(Entity entity) {
    shiftSubtreeTracked(entity, 1);
}

void World::untrackEntity(Entity entity) {
    shiftSubtreeTracked(entity, -1);
}

int32_t World::subtreeTracked(UUID entityId) const {
    const auto it = m_subtreeTracked.find(entityId);
    return it != m_subtreeTracked.end() ? it->second : 0;
}

void World::shiftSubtreeTracked(Entity entity, int32_t delta) {
    if (delta == 0) { return; }
    // Up the hierarchy as the registry holds it: a tombstone hides an entity from the world,
    // and the tracked entities under it are taken out while it is still above them.
    entt::entity handle = entity.m_handle;
    while (m_registry.valid(handle)) {
        const auto it = m_subtreeTracked.try_emplace(m_registry.get<IdComponent>(handle).id, 0).first;
        it->second += delta;
        PND_ASSERT(it->second >= 0, "MORE ENTITIES UNTRACKED IN A SUBTREE THAN TRACKED");
        if (it->second == 0) { m_subtreeTracked.erase(it); }
        const auto parent = m_entityIdMap.find(m_registry.get<RelationshipComponent>(handle).parent);
        handle = parent != m_entityIdMap.end() ? parent->second.m_handle : entt::null;
    }
}

void World::destroyPending() {
    if (m_pendingDestroy.empty()) { return; }
    // The list is taken as a whole: a script's shutdown may ask for more, and that is for the
    // next time. An entity asked for twice, or gone with its parent, has a dead handle by now.
    std::vector<entt::entity> pending;
    pending.swap(m_pendingDestroy);
    for (entt::entity handle : pending) {
        if (m_registry.valid(handle)) { destroyNow(Entity(handle, this)); }
    }
}

void World::clear() {
    m_commandHistory.CLEAR();
    m_changed = false;
    std::vector<UUID> entityIds;
    entityIds.reserve(m_entityIdMap.size());
    for (const auto &[entityId, entity] : m_entityIdMap) {
        entityIds.push_back(entityId);
    }
    if (ExternalCalls::entityWillBeDeleted) {
        for (UUID entityId : entityIds) {
            ExternalCalls::entityWillBeDeleted(entityId);
        }
    }
    m_entityIdMap.clear();
    m_pendingDestroy.clear();
    m_registry.clear(); // together with the tombstones
    // An empty world has the default settings and no sections of modules that are not loaded;
    // the state of the systems in the context is theirs and stays.
    SettingsMapper::reset(SettingsScope::World, m_context);
    bumpStructureVersion();
}

bool World::isEmpty() {
    return !m_registry.storage<entt::entity>().size();
}

Entity World::findMainCameraEntity() {
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view) {
        if (!isValidEntt(entity)) { continue; }
        auto &comp = view.get<CameraComponent>(entity);
        if (comp.isPrimary) { return {entity, this}; }
    }
    return {};
}

Camera *World::findMainCamera() {
    Entity entity = findMainCameraEntity();
    if (!entity.isValid()) { return nullptr; }
    return &entity.getComponent<CameraComponent>().camera;
}

Entity World::findByTag(const char *tag) {
    auto view = m_registry.view<TagComponent>();
    for (auto entityHandle : view) {
        if (!isValidEntt(entityHandle)) { continue; }
        auto &tagComponent = view.get<TagComponent>(entityHandle);
        if (tagComponent.tag == tag) { return Entity(entityHandle, this); }
    }
    return Entity();
}

std::vector<Entity> World::findAllByTag(const char *tag) {
    std::vector<Entity> entities;
    auto view = m_registry.view<TagComponent>();
    for (auto entityHandle : view) {
        if (!isValidEntt(entityHandle)) { continue; }
        auto &tagComponent = view.get<TagComponent>(entityHandle);
        if (tagComponent.tag == tag) { entities.push_back(Entity(entityHandle, this)); }
    }
    return entities;
}

Entity World::getById(UUID id) {
    // "Not found" is a legal answer of a live world: scripts delete entities at any moment,
    // while the selection/panels may hold the UUID for one more frame. Callers check isValid().
    auto it = m_entityIdMap.find(id);
    return it != m_entityIdMap.end() ? it->second : Entity();
}

std::unordered_set<Entity> World::getById(std::unordered_set<UUID> ids) {
    std::unordered_set<Entity> result;
    for (UUID id : ids) {
        Entity entity = getById(id);
        if (entity.isValid()) { result.emplace(entity); }
    }
    return result;
}

glm::mat4 World::getWorldSpaceTransformMatrix(Entity entity) {
    glm::mat4 result(1.0f);
    Entity parent = entity.getParent();
    if (parent) { result = getWorldSpaceTransformMatrix(parent); }
    return result * entity.getTransform().getLocalTransform();
}

World &World::operator=(World &other) {
    m_entityIdMap.clear();
    clear();
    // The receiver's pools follow the current type registry: the project modules could have
    // registered components after this world was created (the editor's playing world lives since
    // the editor start).
    prepareComponentTypes();
    // The settings of the world scope, the core's and the modules' alike (the wind too: the layer
    // applies the current world's wind, Play does not reset it).
    SettingsMapper::copy(SettingsScope::World, other.m_context, m_context);
    entt::registry &src = other.m_registry;
    entt::registry &dst = m_registry;
    for (auto entityHandle : src.view<entt::entity>()) {
        auto _ = dst.create(entityHandle);
        UUID id = src.get<IdComponent>(entityHandle).id;
        m_entityIdMap[id] = Entity(entityHandle, this);
    }
    // All pools without types (entt: push copies by a pointer to the value, tags have none);
    // the receiver's pool is created in World::World from the component type registry.
    for (auto [id, storage] : src.storage()) {
        auto *target = dst.storage(id);
        PND_ASSERT_F(
            target != nullptr, "COMPONENT POOL %u IS NOT REGISTERED (Extensions::components)",
            static_cast<uint32_t>(id)
        );
        for (auto entityHandle : storage) {
            target->push(entityHandle, storage.value(entityHandle));
        }
    }
    sort();
    return *this;
}

Entity World::duplicateEntity(Entity entity) {
    if (!entity) { return {}; }
    Entity duplicate = duplicateSubtree(entity);
    // Next to the source: under the same parent, with the same transform.
    Entity parent = entity.getParent();
    if (parent) { attach(duplicate, parent); }
    // A running world: the duplicate and everything under it appear with their components at
    // once, past Entity::addComponent — the systems hear about them as about restored entities
    // (bodies are created), once the whole subtree stands in the hierarchy.
    if (m_isRunning) { restoreSubtree(duplicate); }
    bumpStructureVersion();
    m_changed = true;
    return duplicate;
}

void World::restoreSubtree(Entity entity) {
    runEntityHooks(entity, false);
    for (UUID childId : entity.getChildEntities()) {
        if (isValid(childId)) { restoreSubtree(getById(childId)); }
    }
}

Entity World::duplicateSubtree(Entity entity) {
    const entt::entity src = entity.m_handle;
    const entt::entity dst = m_registry.create();
    m_registry.emplace<IdComponent>(dst);
    m_registry.emplace<RelationshipComponent>(dst);
    Entity duplicate = {dst, this};
    // Components by the type registry: Id and Relationship are special (a new id, and the
    // hierarchy under the duplicate is built below).
    for (const ComponentType &type : GameContext::getExtensions().components.types()) {
        if (!type.duplicated) { continue; }
        auto *pool = m_registry.storage(type.typeId);
        if (pool == nullptr || !pool->contains(src)) { continue; }
        pool->push(dst, pool->value(src));
    }
    m_entityIdMap[duplicate.getId()] = duplicate;
    // The children of the source in their order, with their transforms as they are; a deleted
    // child is not copied.
    const std::vector<UUID> children = entity.getChildEntities();
    for (UUID childId : children) {
        if (isValid(childId)) { attach(duplicateSubtree(getById(childId)), duplicate); }
    }
    return duplicate;
}

bool World::isValidEntt(entt::entity handle) {
    return m_registry.valid(handle) && !isDeleted(handle);
}

bool World::isValid(UUID entityId) {
    const auto it = m_entityIdMap.find(entityId);
    return it != m_entityIdMap.end() && !isDeleted(it->second.m_handle);
}

void World::convertToLocalSpace(Entity entity) {
    Entity parent = entity.getParent();
    if (!parent) { return; }
    auto &transformComponent = entity.getTransform();
    glm::mat4 parentTransform = getWorldSpaceTransformMatrix(parent);
    glm::mat4 localTransform = glm::inverse(parentTransform) * transformComponent.getLocalTransform();
    transformComponent.setTransform(localTransform);
}

void World::convertToWorldSpace(Entity entity) {
    Entity parent = entity.getParent();
    if (!parent) { return; }
    glm::mat4 transform = getWorldSpaceTransformMatrix(entity);
    auto &transformComponent = entity.getTransform();
    transformComponent.setTransform(transform);
}

void World::setWorldPose(Entity entity, glm::vec3 position, glm::quat rotation) {
    Entity parent = entity.getParent();
    if (parent) {
        const glm::mat4 parentTransform = getWorldSpaceTransformMatrix(parent);
        // A parent squashed flat has no space of its own to put the pose into.
        if (glm::determinant(glm::mat3(parentTransform)) == 0.f) { return; }
        const glm::mat4 toParent = glm::inverse(parentTransform);
        position = glm::vec3(toParent * glm::vec4(position, 1.f));
        // The local rotation that the world matrix of the entity gives back as the asked one,
        // whatever the scale of the parents is.
        rotation = rotationOf(glm::mat3(toParent) * glm::mat3_cast(rotation));
    }
    TransformComponent &transformComponent = entity.getTransform();
    transformComponent.setPosition(position);
    transformComponent.setRotation(rotation);
}

void World::sort() {
    m_registry.sort<entt::entity>([this](auto l, auto r) {
        Entity le = {l, this};
        Entity re = {r, this};
        bool lch = le.hasAnyChild();
        bool rch = re.hasAnyChild();
        if (lch == rch) { return le.getName() < re.getName(); }
        return lch > rch;
    });
    auto view = m_registry.view<RelationshipComponent>();
    for (auto entityId : view) {
        auto &children = view.get<RelationshipComponent>(entityId).children;
        std::sort(children.begin(), children.end(), [this](auto l, auto r) {
            Entity le = getById(l);
            Entity re = getById(r);
            bool lch = le.hasAnyChild();
            bool rch = re.hasAnyChild();
            if (lch == rch) { return le.getName() < re.getName(); }
            return lch > rch;
        });
    }
}

bool World::needToDestroy(Entity entity) {
    PND_ASSERT(entity.getWorld() == this, "WRONG WORLD INSTANCE");
    return m_registry.valid(entity.m_handle) && m_registry.any_of<DeletedComponent>(entity.m_handle);
}

bool World::isDeleted(entt::entity handle) {
    if (!m_registry.valid(handle)) { return true; }
    if (m_registry.any_of<DeletedComponent>(handle)) { return true; }
    const RelationshipComponent &relationship = m_registry.get<RelationshipComponent>(handle);
    if (relationship.parent) {
        const auto parent = m_entityIdMap.find(relationship.parent);
        return parent == m_entityIdMap.end() || isDeleted(parent->second.m_handle);
    }
    return false;
}

} // namespace Panda
