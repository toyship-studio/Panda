#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

namespace Panda {

class UpdateRuntimeMeshCommand : public WorldCommand {
public:
    UpdateRuntimeMeshCommand(const std::unordered_set<Entity> &entities)
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
        UpdateRuntimeMeshCommand &other = static_cast<UpdateRuntimeMeshCommand &>(command);
        return other.m_entities == m_entities;
    }

    void merge(WorldCommand &command) override {
        UpdateRuntimeMeshCommand &cmd = static_cast<UpdateRuntimeMeshCommand &>(command);
        m_newComponent = cmd.m_newComponent;
    }

    void saveBeforeEdit() {
        for (auto entity : m_entities) {
            m_prevComponent[entity.getId()] = entity.getComponent<RuntimeMeshComponent>();
        }
    }

    void saveAfterEdit() {
        for (auto entity : m_entities) {
            m_newComponent[entity.getId()] = entity.getComponent<RuntimeMeshComponent>();
        }
    }

private:
    std::unordered_set<Entity> m_entities;
    std::unordered_map<UUID, RuntimeMeshComponent> m_newComponent;
    std::unordered_map<UUID, RuntimeMeshComponent> m_prevComponent;
};

} // namespace Panda