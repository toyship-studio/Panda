#include "Panda/GameLogic/World.hpp"
#include "Panda/GameLogic/Input.hpp"
#include "Panda/GameLogic/Systems/RenderSystem.hpp"
#include "Panda/GameLogic/Systems/ScriptSystem.hpp"
#include <Foundation/Profile.hpp>
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Prefabs/PrefabAsset.hpp"
#include "Panda/Prefabs/PrefabComponent.hpp"

#include <Rain/Rain.hpp>
#include <entt/entt.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Panda {

namespace {
    // The prefabs a running world made instances of, held loaded while it runs: an asset nobody
    // refers to is freed, and a game that spawns one prefab over and over would read it again
    // for every instance.
    struct InstantiatedPrefabs {
        std::unordered_map<AssetId, AssetRef<PrefabAsset>> held;
    };

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
        GameContext::s_runningWorld = nullptr;
    }
    // The commands of the undo history hold entities of this world, and a command destroys the
    // tombstone it kept for undo: they go while the registry is alive (the members are destroyed
    // in the reverse order — the registry before the history).
    m_commandHistory.clear();
}

void World::startRunning() {
    // One world runs at a time (GameContext::getRunningWorld): the script bundle and the script
    // hosts of the modules address it from the first callback of the start to the last one of
    // the stop.
    PND_ASSERT(GameContext::s_runningWorld == nullptr, "A WORLD IS ALREADY RUNNING: ONE WORLD RUNS AT A TIME");
    GameContext::s_runningWorld = this;
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
    GameContext::s_runningWorld = nullptr;
    eraseContext<InstantiatedPrefabs>();
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

void World::update(double deltaTime) {
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

UUID World::newEntityId() {
    AssetHandler *assets = GameContext::getAssetHandler();
    for (;;) {
        const UUID id; // a random number
        if (id != UUID(0) && !m_entityIdMap.contains(id) && !(assets != nullptr && assets->hasAsset(id))) { return id; }
    }
}

bool World::changeEntityId(Entity entity, UUID id) {
    PND_ASSERT(!m_isRunning, "THE ID OF AN ENTITY CHANGES IN A WORLD THAT DOES NOT RUN");
    const UUID old = entity.getId();
    if (id == old) { return true; }
    if (id == UUID(0) || m_entityIdMap.contains(id)) { return false; }
    m_registry.get<IdComponent>(entity.m_handle).id = id;
    m_entityIdMap.erase(old);
    m_entityIdMap[id] = entity;
    // The two sides of its links.
    const RelationshipComponent &relationship = m_registry.get<RelationshipComponent>(entity.m_handle);
    if (const auto parent = m_entityIdMap.find(relationship.parent); parent != m_entityIdMap.end()) {
        for (UUID &child : m_registry.get<RelationshipComponent>(parent->second.m_handle).children) {
            if (child == old) { child = id; }
        }
    }
    for (UUID childId : relationship.children) {
        if (const auto child = m_entityIdMap.find(childId); child != m_entityIdMap.end()) {
            m_registry.get<RelationshipComponent>(child->second.m_handle).parent = id;
        }
    }
    // Whoever names it in a component.
    const ComponentTypeRegistry &types = GameContext::getExtensions().components;
    for (auto handle : m_registry.view<IdComponent>()) {
        types.forEachReference(Entity(handle, this), [old, id](UUID &reference) {
            if (reference == old) { reference = id; }
        });
    }
    bumpStructureVersion();
    setChanged();
    return true;
}

Entity World::instantiateEntity() {
    entt::entity entityHandle = m_registry.create();
    Entity entity = {entityHandle, this};
    fillEntity(entity, newEntityId());
    m_entityIdMap[entity.getId()] = entity;
    bumpStructureVersion();
    setChanged();
    return entity;
}

Entity World::instantiateEntity(UUID id) {
    entt::entity entityHandle = m_registry.create();
    Entity entity = {entityHandle, this};
    fillEntity(entity, id);
    bumpStructureVersion();
    m_entityIdMap[entity.getId()] = entity;
    setChanged();
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
    // scale: the bodies under the entity are rebuilt where they stand now. With the world pose
    // kept the bodies stay put, but a collider that lends its shape to the body above it may
    // have changed bodies: the systems hear of the move either way.
    transformChanged(entity, !keepWorldTransform);
    bumpStructureVersion();
    setChanged();
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
    setChanged();
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
    // In a running world the script instances of the entity and the state the systems keep for
    // it leave with it. A world that does not run has neither: the instances of the script
    // bundle are the running world's, and another world may hold an entity with the same id.
    if (m_isRunning) {
        if (ExternalCalls::entityWillBeDeleted) { ExternalCalls::entityWillBeDeleted(entityId); }
        runEntityHooks(entity, /*removed*/ true);
    }
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
    m_commandHistory.clear();
    m_changed = false;
    std::vector<UUID> entityIds;
    entityIds.reserve(m_entityIdMap.size());
    for (const auto &[entityId, entity] : m_entityIdMap) {
        entityIds.push_back(entityId);
    }
    if (m_isRunning && ExternalCalls::entityWillBeDeleted) { // the script instances are the running world's
        for (UUID entityId : entityIds) {
            ExternalCalls::entityWillBeDeleted(entityId);
        }
    }
    m_entityIdMap.clear();
    m_pendingDestroy.clear();
    m_registry.clear(); // together with the tombstones; the state of the systems in the context is theirs and stays
    bumpStructureVersion();
}

bool World::isEmpty() {
    return !m_registry.storage<entt::entity>().size();
}

Entity World::findMain(entt::id_type typeId, bool (*isMain)(Entity)) {
    const entt::sparse_set *storage = m_registry.storage(typeId);
    if (storage == nullptr) { return {}; }
    for (entt::entity handle : *storage) {
        if (isValidEntt(handle) && isMain(entity(handle))) { return entity(handle); }
    }
    return {};
}

SceneWindSettings World::wind() {
    SceneWindSettings settings;
    const Entity main = findMain<WindComponent>();
    if (!main.isValid()) {
        settings.strength = 0.f; // calm
        return settings;
    }
    const WindComponent &wind = main.getComponent<WindComponent>();
    settings.strength = wind.strength;
    settings.speed = wind.speed;
    settings.gustiness = wind.gustiness;
    settings.directionDegrees = wind.directionDegrees;
    return settings;
}

Entity World::findMain(const ComponentType &type) {
    return type.isMain != nullptr ? findMain(type.typeId, type.isMain) : Entity();
}

std::vector<Entity> World::findMains(const ComponentType &type) {
    std::vector<Entity> mains;
    const entt::sparse_set *storage = m_registry.storage(type.typeId);
    if (storage == nullptr || type.isMain == nullptr) { return mains; }
    for (entt::entity handle : *storage) { // the order findMain looks in
        if (isValidEntt(handle) && type.isMain(entity(handle))) { mains.push_back(entity(handle)); }
    }
    return mains;
}

void World::setMain(const ComponentType &type, Entity main) {
    const entt::sparse_set *storage = m_registry.storage(type.typeId);
    if (storage == nullptr || type.setMain == nullptr) { return; }
    const std::vector<entt::entity> handles(storage->begin(), storage->end()); // setMain writes the pool
    for (entt::entity handle : handles) { type.setMain(entity(handle), handle == main.m_handle); }
    setChanged();
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

std::vector<Entity> World::subtree(Entity entity, const std::function<bool(Entity)> &skip) {
    std::vector<Entity> entities;
    if (!entity.isValid()) { return entities; }
    std::vector<Entity> pending{entity};
    while (!pending.empty()) {
        Entity at = pending.back();
        pending.pop_back();
        entities.push_back(at);
        const std::vector<UUID> &children = at.getChildEntities();
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            Entity below = getById(*child);
            if (below.isValid() && (!skip || !skip(below))) { pending.push_back(below); }
        }
    }
    return entities;
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
    return *this;
}

Entity World::duplicateEntity(Entity entity, const EntityPlacement &placement) {
    if (!entity) { return {}; }
    std::vector<Entity> copies;
    Entity duplicate = copySubtree(*this, entity, AssetId(0), UUID(0), copies);
    // Next to the source — under the same parent, with the same transform — unless the
    // placement says otherwise.
    settle(duplicate, copies, placement.parent ? placement.parent : entity.getParent(), placement);
    return duplicate;
}

Entity World::importEntity(
    World &source, Entity entity, const EntityPlacement &placement, std::unordered_map<UUID, UUID> *ids,
    const std::function<bool(Entity)> &skip
) {
    if (!entity) { return {}; }
    std::vector<Entity> copies;
    Entity imported = copySubtree(source, entity, AssetId(0), UUID(0), copies, ids, skip);
    settle(imported, copies, placement.parent, placement);
    return imported;
}

Entity World::instantiate(const AssetRef<PrefabAsset> &prefab, UUID rootId, const EntityPlacement &placement) {
    PrefabAsset *asset = prefab.get();
    Entity root = asset != nullptr ? asset->root() : Entity();
    if (!root.isValid()) { return {}; }
    if (m_isRunning) { context<InstantiatedPrefabs>().held.try_emplace(prefab.getId(), prefab); }
    std::vector<Entity> copies;
    Entity instance = copySubtree(asset->world(), root, prefab.getId(), rootId, copies);
    settle(instance, copies, placement.parent, placement);
    return instance;
}

void World::joinAsSpares(const std::vector<Entity> &copies) {
    for (const ComponentType &type : GameContext::getExtensions().components.types()) {
        if (type.isMain == nullptr || type.setMain == nullptr) { continue; }
        const entt::sparse_set *storage = m_registry.storage(type.typeId);
        if (storage == nullptr) { continue; }
        // Most copies have no component of such a kind: nothing to look at.
        std::vector<Entity> joined;
        for (Entity copy : copies) {
            if (storage->contains(copy.m_handle) && type.isMain(copy)) { joined.push_back(copy); }
        }
        if (joined.empty()) { continue; }
        bool hasMain = false; // the world has a main one that is not among the copies
        for (entt::entity handle : *storage) {
            if (!isValidEntt(handle) || !type.isMain(entity(handle))) { continue; }
            const bool copy = std::any_of(joined.begin(), joined.end(), [handle](const Entity &each) {
                return each.m_handle == handle;
            });
            if (!copy) {
                hasMain = true;
                break;
            }
        }
        for (Entity copy : joined) {
            if (hasMain) { type.setMain(copy, false); }
            hasMain = true; // the first of the copies is the main one of a world that had none
        }
    }
}

void World::settle(Entity root, const std::vector<Entity> &copies, Entity parent, const EntityPlacement &placement) {
    joinAsSpares(copies);
    if (parent) { attach(root, parent); }
    if (placement.position) { root.getTransform().setPosition(*placement.position); }
    if (placement.rotation) { root.getTransform().setRotation(*placement.rotation); }
    bumpStructureVersion();
    setChanged();
    // A running world: the copies appear with their components at once, past
    // Entity::addComponent. The systems hear about them as about restored entities (bodies are
    // created) once the whole subtree stands in the hierarchy, then their scripts are created
    // and started.
    if (!m_isRunning) { return; }
    restoreSubtree(root);
    ScriptSystem::instantiate(*this, copies);
    ScriptSystem::start(copies);
}

void World::restoreSubtree(Entity entity) {
    runEntityHooks(entity, false);
    for (UUID childId : entity.getChildEntities()) {
        if (isValid(childId)) { restoreSubtree(getById(childId)); }
    }
}

Entity World::copySubtree(
    World &source, Entity entity, AssetId prefab, UUID rootId, std::vector<Entity> &copies, std::unordered_map<UUID, UUID> *idsOut,
    const std::function<bool(Entity)> &skip
) {
    const ComponentTypeRegistry &types = GameContext::getExtensions().components;
    // What is copied: the entity and everything under it, a parent before its children; a
    // deleted child is not copied.
    std::vector<Entity> originals = source.subtree(entity, skip);

    // The ids of the copies. An entity of an instance of a prefab — of the one being made, or of
    // one that is copied whole — has the id derived from the root of its instance; everything
    // else gets a new one.
    std::unordered_map<UUID, UUID> ids; // the id of an original → the id of its copy
    std::unordered_set<UUID> given;
    const auto taken = [&](UUID id) { return id == UUID(0) || m_entityIdMap.contains(id) || given.contains(id); };
    const auto give = [&](Entity original, UUID id) {
        ids[original.getId()] = id;
        given.insert(id);
    };
    struct Part {
        Entity original;
        UUID instance; // the original root of its instance
        UUID source;
    };
    std::vector<Part> parts;
    if (prefab != AssetId(0)) {
        give(entity, rootId != UUID(0) ? rootId : newEntityId());
        for (Entity original : originals) {
            if (!(original == entity)) { parts.push_back({original, entity.getId(), original.getId()}); }
        }
    } else {
        std::unordered_set<UUID> copied;
        for (Entity original : originals) { copied.insert(original.getId()); }
        for (Entity original : originals) {
            const PrefabComponent *link = original.hasComponent<PrefabComponent>() ? &original.getComponent<PrefabComponent>() : nullptr;
            // A part whose root is copied too stays a part of the copy of that instance.
            if (link != nullptr && link->instance != original.getId() && copied.contains(link->instance)) {
                parts.push_back({original, link->instance, link->source});
            } else {
                give(original, newEntityId());
            }
        }
    }
    // In the order of the ids, so that the outcome depends on them alone (the reader of a world
    // file derives the same ids for the same instance).
    std::sort(parts.begin(), parts.end(), [](const Part &l, const Part &r) {
        return l.instance != r.instance ? l.instance < r.instance : l.source < r.source;
    });
    for (const Part &part : parts) { give(part.original, freeDerivedEntityId(ids.at(part.instance), part.source, taken)); }

    // The copies: the components by the type registry. Id and Relationship are special (a new
    // id, and the hierarchy under the copy is built below).
    copies.reserve(originals.size());
    for (Entity original : originals) {
        const entt::entity dst = m_registry.create();
        m_registry.emplace<IdComponent>(dst, ids.at(original.getId()));
        m_registry.emplace<RelationshipComponent>(dst);
        for (const ComponentType &type : types.types()) {
            if (!type.duplicated) { continue; }
            auto *from = source.m_registry.storage(type.typeId);
            auto *to = m_registry.storage(type.typeId);
            if (from == nullptr || to == nullptr || !from->contains(original.m_handle)) { continue; }
            to->push(dst, from->value(original.m_handle));
        }
        Entity copy = {dst, this};
        m_entityIdMap[copy.getId()] = copy;
        copies.push_back(copy);
    }
    // The hierarchy under the root, the children in the order of the source, with their
    // transforms as they are.
    for (size_t i = 1; i < originals.size(); i++) {
        attach(copies[i], getById(ids.at(originals[i].getParent().getId())));
    }
    for (size_t i = 0; i < originals.size(); i++) {
        Entity original = originals[i];
        Entity copy = copies[i];
        // Where the copy came from. An instance of a prefab: every copy is a part of it, and the
        // root of an instance nested in the prefab keeps the prefab it is of. A plain copy: a
        // part copied without the root of its instance is a part of nothing.
        if (prefab != AssetId(0)) {
            const AssetId nested = original.hasComponent<PrefabComponent>() ? original.getComponent<PrefabComponent>().prefab : AssetId(0);
            PrefabComponent &link = copy.hasComponent<PrefabComponent>() ? copy.getComponent<PrefabComponent>()
                                                                        : m_registry.emplace<PrefabComponent>(copy.m_handle);
            link.instance = entity.getId(); // the id of the original root: brought to the copy below, as any reference
            link.source = original.getId();
            link.prefab = i == 0 ? prefab : nested;
        } else if (copy.hasComponent<PrefabComponent>()) {
            const PrefabComponent &link = copy.getComponent<PrefabComponent>();
            if (!ids.contains(link.instance)) { m_registry.remove<PrefabComponent>(copy.m_handle); }
        }
        // A reference between two copied entities names the copies.
        types.forEachReference(copy, [&ids](UUID &reference) {
            const auto mapped = ids.find(reference);
            if (mapped != ids.end()) { reference = mapped->second; }
        });
        // The script instances of the original are its own: the copy gets none until it runs.
        for (ExternalScript &script : copy.getComponent<ScriptListComponent>().scripts) { script.unbind(); }
    }
    if (idsOut != nullptr) { *idsOut = std::move(ids); }
    return copies.front();
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

std::vector<UUID> World::orderedChildren(UUID parentId) {
    struct Sibling {
        UUID id;
        bool group;
        const std::string *name;
    };
    std::vector<Sibling> siblings;
    const auto add = [&](entt::entity handle) {
        if (handle == entt::null || !isValidEntt(handle)) { return; }
        Entity entity(handle, this);
        siblings.push_back({entity.getId(), entity.hasAnyChild(), &m_registry.get<TagComponent>(handle).tag});
    };
    if (parentId == UUID(0)) {
        auto view = m_registry.view<IdComponent, RelationshipComponent, TagComponent>();
        for (entt::entity handle : view) {
            if (view.get<RelationshipComponent>(handle).parent == UUID(0)) { add(handle); }
        }
    } else {
        const auto parent = m_entityIdMap.find(parentId);
        if (parent == m_entityIdMap.end()) { return {}; }
        for (UUID childId : m_registry.get<RelationshipComponent>(parent->second.m_handle).children) {
            const auto child = m_entityIdMap.find(childId);
            if (child != m_entityIdMap.end()) { add(child->second.m_handle); }
        }
    }
    std::sort(siblings.begin(), siblings.end(), [](const Sibling &l, const Sibling &r) {
        if (l.group != r.group) { return l.group; }
        const int names = l.name->compare(*r.name);
        return names != 0 ? names < 0 : l.id < r.id;
    });
    std::vector<UUID> ordered;
    ordered.reserve(siblings.size());
    for (const Sibling &sibling : siblings) { ordered.push_back(sibling.id); }
    return ordered;
}

size_t World::repairHierarchy() {
    size_t repaired = 0;
    auto view = m_registry.view<IdComponent, RelationshipComponent>();
    // The parents first: a parent id without an entity, or a chain of parents that comes back to
    // the entity (the walk never ends in isDeleted and the transforms), is cut at the entity.
    for (auto handle : view) {
        const UUID id = view.get<IdComponent>(handle).id;
        RelationshipComponent &relationship = view.get<RelationshipComponent>(handle);
        if (relationship.parent == 0) { continue; }
        if (!m_entityIdMap.contains(relationship.parent)) {
            LOG_WARN(
                "World: entity %u names the parent %u, which does not exist; it is a root now", (uint32_t)id,
                (uint32_t)relationship.parent
            );
            relationship.parent = 0;
            repaired++;
            continue;
        }
        UUID above = relationship.parent;
        size_t steps = 0;
        while (above != 0 && above != id && steps <= m_entityIdMap.size()) {
            const auto it = m_entityIdMap.find(above);
            if (it == m_entityIdMap.end()) { break; }
            above = m_registry.get<RelationshipComponent>(it->second.m_handle).parent;
            steps++;
        }
        if (above == id || steps > m_entityIdMap.size()) {
            LOG_WARN("World: entity %u is its own ancestor through the parents; it is a root now", (uint32_t)id);
            relationship.parent = 0;
            repaired++;
        }
    }
    // Then the children lists, against the parent fields: a listed id without an entity, or of an
    // entity under another parent, leaves; an entity under this one that is not listed is added.
    for (auto handle : view) {
        const UUID id = view.get<IdComponent>(handle).id;
        std::vector<UUID> &children = view.get<RelationshipComponent>(handle).children;
        for (auto it = children.begin(); it != children.end();) {
            const auto child = m_entityIdMap.find(*it);
            if (child == m_entityIdMap.end()) {
                LOG_WARN(
                    "World: entity %u lists the child %u, which does not exist; the link is dropped", (uint32_t)id,
                    (uint32_t)*it
                );
                it = children.erase(it);
                repaired++;
            } else if (m_registry.get<RelationshipComponent>(child->second.m_handle).parent != id) {
                LOG_WARN(
                    "World: entity %u lists the child %u, which is under another parent; the link is dropped",
                    (uint32_t)id, (uint32_t)*it
                );
                it = children.erase(it);
                repaired++;
            } else {
                ++it;
            }
        }
    }
    for (auto handle : view) {
        const UUID id = view.get<IdComponent>(handle).id;
        const UUID parentId = view.get<RelationshipComponent>(handle).parent;
        if (parentId == 0) { continue; }
        const entt::entity parentHandle = m_entityIdMap.at(parentId).m_handle;
        std::vector<UUID> &siblings = m_registry.get<RelationshipComponent>(parentHandle).children;
        if (std::find(siblings.begin(), siblings.end(), id) == siblings.end()) {
            LOG_WARN(
                "World: entity %u is under %u, which did not list it; the link is added", (uint32_t)id, (uint32_t)parentId
            );
            siblings.push_back(id);
            repaired++;
        }
    }
    return repaired;
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
