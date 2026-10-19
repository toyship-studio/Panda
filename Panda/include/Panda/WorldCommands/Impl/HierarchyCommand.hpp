#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/World.hpp"

namespace Panda {

class HierarchyCommand : public WorldCommand {
public:
    HierarchyCommand(const std::vector<Entity> &entities, Entity newParent)
        : m_entities(entities)
        , m_newParent(newParent) {
        PND_ASSERT(!entities.empty(), "Entities should not be empty");
        for (auto entity : entities) {
            m_prevParents[entity.getId()] = entity.getParent();
        }
    }

    // A parent that is not valid (there was none, or it is deleted by now) makes a root.
    bool undo() override {
        for (auto entity : m_entities) {
            entity.getWorld()->setParent(entity, m_prevParents.at(entity.getId()));
        }
        m_entities[0].sortWorld();
        m_entities[0].setWorldChanged();
        return true;
    }

    bool execute() override {
        for (auto entity : m_entities) {
            entity.getWorld()->setParent(entity, m_newParent);
        }
        m_entities[0].sortWorld();
        m_entities[0].setWorldChanged();
        return true;
    }

    bool canMerge(WorldCommand &command) override {
        return false;
    }

private:
    std::vector<Entity> m_entities;
    Entity m_newParent;
    std::unordered_map<UUID, Entity> m_prevParents;
};

} // namespace Panda