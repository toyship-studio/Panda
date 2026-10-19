//
// Created by Admin on 09.02.2022.
//

#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/World.hpp"

namespace Panda {

Entity::Entity(entt::entity handle, World *world)
    : m_handle(handle)
    , m_world(world) {}

Entity::Entity()
    : m_handle(entt::null)
    , m_world(nullptr) {}

entt::registry &Entity::worldGetRegistry() const {
    return m_world->m_registry;
}

bool Entity::hasComponent(entt::id_type typeId) const {
    if (m_handle == entt::null || !m_world) { return false; }
    entt::registry &registry = worldGetRegistry();
    if (!registry.valid(m_handle)) { return false; }
    const auto *pool = registry.storage(typeId);
    return pool != nullptr && pool->contains(m_handle);
}

TransformComponent &Entity::getTransform() {
    return getComponent<TransformComponent>();
}

TransformComponent Entity::calculateWorldSpaceTransform() {
    TransformComponent transformComponent;
    glm::mat4 transform = m_world->getWorldSpaceTransformMatrix(*this);
    transformComponent.setTransform(transform);
    return transformComponent;
}

// Whether there is a live (not deleted) child: the tombstones of children do not count.
bool Entity::hasAnyChild() {
    for (UUID childId : getChildEntities()) {
        const Entity child = m_world->getById(childId);
        PND_ASSERT(child.m_handle != entt::null, "ENTITY DELETED");
        if (!worldGetRegistry().any_of<DeletedComponent>(child.m_handle)) { return true; }
    }
    return false;
}

bool Entity::isAncestorOf(Entity entity) {
    if (entity.m_handle == entt::null) { return false; }
    // Up from the entity: this one has to be on the way.
    for (Entity above = entity.getParent(); above.m_handle != entt::null; above = above.getParent()) {
        if (above == *this) { return true; }
    }
    return false;
}

bool Entity::isDescendantOf(Entity entity) {
    return entity.isAncestorOf(*this);
}

Entity Entity::getParent() {
    const UUID parent = getComponent<RelationshipComponent>().getParent();
    if (!parent) { return {}; }
    return m_world->getById(parent);
}

bool Entity::isValid() const {
    return (m_handle != entt::null) && m_world && m_world->isValidEntt(m_handle);
}

Entity::operator bool() const {
    return isValid();
}

void Entity::componentsChanged(entt::id_type typeId) {
    m_world->componentsChanged(*this, typeId);
}

void Entity::transformUpdated() {
    m_world->transformChanged(*this, false);
}

void Entity::scaleUpdated() {
    m_world->transformChanged(*this, true);
}

void Entity::setWorldChanged(bool changed) {
    m_world->setChanged(changed);
}

bool Entity::needToDestroy() const {
    return m_world->needToDestroy(*this);
}

bool Entity::isDeleted() const {
    return m_world->isDeleted(m_handle);
}

void Entity::sortWorld() const {
    m_world->sort();
}

void Entity::setDeleted(bool deleted) {
    if (deleted == hasComponent<DeletedComponent>()) { return; }
    if (deleted) {
        addTag<DeletedComponent>();
    } else {
        removeComponent<DeletedComponent>();
    }
    m_world->deletionChanged(*this);
}

} // namespace Panda
