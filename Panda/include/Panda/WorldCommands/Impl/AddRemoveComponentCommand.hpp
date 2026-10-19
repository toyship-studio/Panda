#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

#include <type_traits>

namespace Panda {

// Toggle of component T on a set of entities: all have it — remove (the value is remembered
// for undo), otherwise — add to those that lack it. A tag (empty T) carries no value.
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

    bool canMerge(WorldCommand &command) override {
        if (typeid(command) != typeid(*this)) { return false; }
        AddRemoveComponentCommand &other = static_cast<AddRemoveComponentCommand &>(command);
        return other.m_entities == m_entities;
    }

private:
    std::unordered_set<Entity> m_entities;
    T component;
};

} // namespace Panda