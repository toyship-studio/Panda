#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

namespace Panda {

class UpdateImportedMeshCommand : public WorldCommand {
public:
    UpdateImportedMeshCommand(const std::unordered_set<Entity> &entities)
        : m_entities(entities)
        , m_newComponent()
        , m_prevComponent() {}

    bool undo() override {
        for (Entity entity : m_entities) {
            if (!entity.isValid()) { return false; }
            entity.setComponent(m_prevComponent[entity.getId()]);
            entity.setWorldChanged();
        }
        return true;
    }

    bool execute() override {
        for (Entity entity : m_entities) {
            if (!entity.isValid()) { return false; }
            entity.setComponent(m_newComponent[entity.getId()]);
            entity.setWorldChanged();
        }
        return true;
    }

    bool canMerge(WorldCommand &command) override {
        if (typeid(command) != typeid(*this)) { return false; }
        UpdateImportedMeshCommand &other = static_cast<UpdateImportedMeshCommand &>(command);
        return other.m_entities == m_entities;
    }

    void merge(WorldCommand &command) override {
        UpdateImportedMeshCommand &cmd = static_cast<UpdateImportedMeshCommand &>(command);
        m_newComponent = cmd.m_newComponent;
    }

    void saveBeforeEdit() {
        for (auto entity : m_entities) {
            m_prevComponent[entity.getId()] = entity.getComponent<ImportedSubmeshComponent>();
        }
    }

    void saveAfterEdit() {
        for (auto entity : m_entities) {
            m_newComponent[entity.getId()] = entity.getComponent<ImportedSubmeshComponent>();
        }
    }

private:
    std::unordered_set<Entity> m_entities;
    std::unordered_map<UUID, ImportedSubmeshComponent> m_newComponent;
    std::unordered_map<UUID, ImportedSubmeshComponent> m_prevComponent;
};

} // namespace Panda