#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"
#include "Panda/GameLogic/Entity.hpp"

namespace Panda {

class UpdateDirectionalLightCommand : public WorldCommand {
public:
    UpdateDirectionalLightCommand(const std::unordered_set<Entity> &entities)
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
        UpdateDirectionalLightCommand &other = static_cast<UpdateDirectionalLightCommand &>(command);
        return other.m_entities == m_entities;
    }

    void merge(WorldCommand &command) override {
        UpdateDirectionalLightCommand &cmd = static_cast<UpdateDirectionalLightCommand &>(command);
        m_newComponent = cmd.m_newComponent;
    }

    void saveBeforeEdit() {
        for (auto entity : m_entities) {
            m_prevComponent[entity.getId()] = entity.getComponent<DirectionalLightComponent>();
        }
    }

    void saveAfterEdit() {
        for (auto entity : m_entities) {
            m_newComponent[entity.getId()] = entity.getComponent<DirectionalLightComponent>();
        }
    }

private:
    std::unordered_set<Entity> m_entities;
    std::unordered_map<UUID, DirectionalLightComponent> m_newComponent;
    std::unordered_map<UUID, DirectionalLightComponent> m_prevComponent;
};

} // namespace Panda
