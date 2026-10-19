//
// Created by Admin on 09.02.2022.
//

#pragma once

#include "Components/TransformComponent.hpp"
#include "Components/BaseComponents.hpp"

#include <entt/entt.hpp>

#include <type_traits>

namespace Panda {

class World;

class Entity final {
private:
    entt::registry &worldGetRegistry() const;

public:
    Entity();

    void addScript(ExternalScript script) {
        ScriptListComponent &component = getComponent<ScriptListComponent>();
        setWorldChanged();
        return component.add(script);
    }

    void removeScript(ExternalScript script) {
        ScriptListComponent &component = getComponent<ScriptListComponent>();
        setWorldChanged();
        return component.remove(script);
    }

    template<typename T, typename... Args>
    T &addComponent(Args &&...args) {
        static_assert(!std::is_empty_v<T>, "Tag component: use addTag<T>()");
        PND_ASSERT(!hasComponent<T>(), "Entity already has component!");
        entt::registry &registry = worldGetRegistry();
        T &component = registry.emplace<T>(m_handle, std::forward<Args>(args)...);
        componentsChanged<T>();
        return component;
    }

    // A tag is an empty component (IsStaticComponent): entt has no instance for it, the entity
    // gets no data. hasComponent/removeComponent for tags are shared with components.
    template<typename T>
    void addTag() {
        static_assert(std::is_empty_v<T>, "Not a tag: use addComponent<T>()");
        PND_ASSERT(!hasComponent<T>(), "Entity already has tag!");
        worldGetRegistry().emplace<T>(m_handle);
        componentsChanged<T>();
    }

    template<typename T>
    void removeComponent() {
        entt::registry &registry = worldGetRegistry();
        registry.remove<T>(m_handle);
        componentsChanged<T>();
    }

    template<typename T>
    T &getComponent() {
        PND_ASSERT(hasComponent<T>(), "Entity doesn't have component!");
        entt::registry &registry = worldGetRegistry();
        return registry.get<T>(m_handle);
    }

    template<typename T>
    T &getComponent() const {
        PND_ASSERT(hasComponent<T>(), "Entity doesn't have component!");
        entt::registry &registry = worldGetRegistry();
        return registry.get<T>(m_handle);
    }

    // Component by type id (entt::type_hash) — for code that does not know the type (the world
    // mapper working from the component type registry).
    bool hasComponent(entt::id_type typeId) const;

    template<typename T>
    bool hasComponent() const {
        // Guard on the PHYSICAL liveness of the handle, not through isValid(): that one reads
        // tombstones (DeletedComponent) — on a half-created entity that is a recursive access, and
        // deleted entities must answer honestly about their components (undo restore).
        if (m_handle == entt::null || !m_world) { return false; }
        entt::registry &registry = worldGetRegistry();
        if (!registry.valid(m_handle)) { return false; }
        return registry.any_of<T>(m_handle);
    }

    // The hierarchy is read here and changed through World::setParent.
    bool hasAnyChild();

    bool isAncestorOf(Entity entity);

    bool isDescendantOf(Entity entity);

    TransformComponent &getTransform();

    TransformComponent calculateWorldSpaceTransform();

    template<typename T>
    void setComponent(T &value) {
        getComponent<T>() = value;
    }

    Entity getParent();

    const std::vector<UUID> &getChildEntities() {
        return getComponent<RelationshipComponent>().getChildren();
    }

    bool isValid() const;

    std::string &getName() const {
        return getComponent<TagComponent>().tag;
    }

    void setName(const std::string &name) {
        auto &tagComponent = getComponent<TagComponent>();
        if (tagComponent.tag == name) { return; }
        tagComponent.tag = name;
        setWorldChanged();
    }

    World *getWorld() {
        return m_world;
    }

    friend bool operator==(const Entity &lhs, const Entity &rhs) {
        return lhs.m_handle == rhs.m_handle;
    }

    operator entt::entity() const {
        return m_handle;
    }
    operator bool() const;

    UUID getId() const {
        return getComponent<IdComponent>().id;
    }

    /// Position/rotation changed from outside the simulation (gizmo/inspector/script): the
    /// running systems move what they keep for the entity and for everything under it
    /// (WorldSystem::transformChanged) to where the transforms put them now.
    void transformUpdated();
    /// The same for a SCALE change: the scale is a part of the shapes.
    void scaleUpdated();

    // World content edit (the "has unsaved changes" flag of World).
    void setWorldChanged(bool changed = true);
    // Tombstone (DeletedComponent): a deleted entity lives in the registry for undo.
    bool needToDestroy() const;
    bool isDeleted() const; // itself or any ancestor
    void sortWorld() const;
    void setDeleted(bool deleted = true);

private:
    Entity(entt::entity handle, World *world);

    // Side effects of a change in the component set: marking the world and telling the running
    // systems (WorldSystem::componentsChanged).
    template<typename T>
    void componentsChanged() {
        setWorldChanged();
        componentsChanged(entt::type_hash<T>::value());
    }
    void componentsChanged(entt::id_type typeId);

    World *m_world;
    entt::entity m_handle;

    friend class World;
    friend class AddRemoveEntitiesCommand;
};

} // namespace Panda

namespace std {

using namespace Panda;

template<>
struct hash<Entity> {
    std::size_t operator()(const Panda::Entity &entity) const {
        return entity.getId();
    }
};

} // namespace std