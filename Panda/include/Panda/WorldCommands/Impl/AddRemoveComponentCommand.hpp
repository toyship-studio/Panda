#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

#include <type_traits>

namespace Panda {

// Toggle of component T on a set of entities: all have it — remove (the value is remembered
// for undo), otherwise — add to those that lack it. A tag (empty T) carries no value. Every
// toggle is a step of the history of its own: adding a component and removing it are two steps.
template<typename T>
class AddRemoveComponentCommand : public WorldCommand {
public:
    AddRemoveComponentCommand(const std::unordered_set<Entity> &entities)
        : m_entities(entities) {}

    bool undo() override {
        return execute();
    }

    bool execute() override {
        bool allEntitiesHaveComponent = true;
        for (Entity entity : m_entities) {
            if (!entity.hasComponent<T>()) {
                allEntitiesHaveComponent = false;
                break;
            }
        }
        for (Entity entity : m_entities) {
            if (!entity.isValid()) { return false; }
            if (allEntitiesHaveComponent) {
                if constexpr (!std::is_empty_v<T>) { component = entity.getComponent<T>(); }
                entity.removeComponent<T>();
            } else if (!entity.hasComponent<T>()) {
                if constexpr (std::is_empty_v<T>) {
                    entity.addTag<T>();
                } else {
                    entity.addComponent<T>(component);
                }
            }
            entity.setWorldChanged();
        }
        return true;
    }

private:
    std::unordered_set<Entity> m_entities;
    T component;
};

} // namespace Panda