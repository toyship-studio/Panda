#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

#include <type_traits>
#include <utility>

namespace Panda {

// Toggle of component T on a set of entities: all have it — remove (the value is remembered
// for undo), otherwise — add to those that lack it, with the value `added` (a component of a kind
// a world uses one of comes as a spare next to the main one). A tag (empty T) carries no value.
// Every toggle is a step of the history of its own: adding a component and removing it are two
// steps.
template<typename T>
class AddRemoveComponentCommand : public WorldCommand {
public:
    explicit AddRemoveComponentCommand(const std::unordered_set<Entity> &entities, T added = T{})
        : m_entities(entities)
        , component(std::move(added)) {}

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