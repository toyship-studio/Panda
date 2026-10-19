#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/GameLogic/Components/TransformComponent.hpp"
#include "Panda/GameLogic/SelectionContext.hpp"

namespace Panda {

class EntityTransformCommand : public WorldCommand {
public:
    EntityTransformCommand(const std::unordered_set<Entity> &entities)
        : m_entities(entities)
        , m_beforeTransforms()
        , m_afterTransforms() {}

    bool undo() override {
        return apply(m_beforeTransforms);
    }

    bool execute() override {
        return apply(m_afterTransforms);
    }

    bool canMerge(WorldCommand &command) override {
        if (typeid(command) != typeid(*this)) { return false; }
        EntityTransformCommand &other = static_cast<EntityTransformCommand &>(command);
        return other.m_entities == m_entities;
    }

    void merge(WorldCommand &command) override {
        EntityTransformCommand &cmd = static_cast<EntityTransformCommand &>(command);
        m_afterTransforms = cmd.m_afterTransforms;
    }

    void saveBeforeEdit() {
        for (auto entity : m_entities) {
            m_beforeTransforms[entity.getId()] = entity.getTransform().getLocalTransform();
        }
    }

    void saveAfterEdit() {
        for (auto entity : m_entities) {
            m_afterTransforms[entity.getId()] = entity.getTransform().getLocalTransform();
        }
    }

private:
    bool apply(std::unordered_map<UUID, glm::mat4> &transforms) {
        PND_ASSERT(!m_entities.empty(), "WRONG TRANSFORM COMMAND DATA");
        bool selectionsUpdated = false;
        for (auto entity : m_entities) {
            if (!entity.isValid()) { return false; }
            auto &transformComponent = entity.getTransform();
            const glm::mat4 previous = transformComponent.getLocalTransform();
            const glm::mat4 next = transforms[entity.getId()];
            transformComponent.setTransform(next);
            entity.setWorldChanged();
            if (scaleChanged(previous, next)) {
                // Scale changes the shape size — a full rebuild of the collider.
                entity.scaleUpdated();
            } else {
                entity.transformUpdated(); // the running systems move what they keep for the entity
            }
            selectionsUpdated |= SelectionContext::isEntitySelected(entity.getId());
        }
        if (selectionsUpdated) { SelectionContext::updateValues(); }
        return true;
    }

    static bool scaleChanged(const glm::mat4 &a, const glm::mat4 &b) {
        for (int axis = 0; axis < 3; axis++) {
            if (glm::abs(glm::length(glm::vec3(a[axis])) - glm::length(glm::vec3(b[axis]))) > 0.0001f) {
                return true;
            }
        }
        return false;
    }

    std::unordered_set<Entity> m_entities;
    std::unordered_map<UUID, glm::mat4> m_beforeTransforms;
    std::unordered_map<UUID, glm::mat4> m_afterTransforms;
};

} // namespace Panda