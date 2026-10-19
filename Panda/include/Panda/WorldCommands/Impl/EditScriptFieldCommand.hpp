#pragma once

#include "Panda/GameLogic/Components/BaseComponents.hpp"
#include "Panda/GameLogic/Entity.hpp"
#include "Panda/ScriptEngine/ExternalScript.hpp"
#include "Panda/WorldCommands/WorldCommand.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace Panda {

// Edit of one field of a script on an entity. The value is written into the world and, while the
// world runs, into the script instance — by execute, undo and redo alike, so the running script
// always holds what the inspector shows. A series of ticks of one field = one command.
class EditScriptFieldCommand : public WorldCommand {
public:
    EditScriptFieldCommand(
        Entity entity, size_t scriptIndex, std::string fieldName, ScriptFieldValue before, ScriptFieldValue after
    )
        : m_entity(entity)
        , m_scriptIndex(scriptIndex)
        , m_fieldName(std::move(fieldName))
        , m_before(before)
        , m_after(after)
        , m_lastEdit(std::chrono::steady_clock::now()) {}

    bool execute() override {
        return apply(m_after);
    }
    bool undo() override {
        return apply(m_before);
    }
    bool canMerge(WorldCommand &command) override {
        auto *other = dynamic_cast<EditScriptFieldCommand *>(&command);
        return other && !m_sealed && other->m_entity == m_entity && other->m_scriptIndex == m_scriptIndex &&
               other->m_fieldName == m_fieldName && withinMergeWindow(m_lastEdit);
    }
    void merge(WorldCommand &command) override {
        m_after = static_cast<EditScriptFieldCommand &>(command).m_after;
        m_lastEdit = std::chrono::steady_clock::now();
    }
    void seal() override {
        m_sealed = true;
    }

private:
    bool apply(const ScriptFieldValue &value) {
        if (!m_entity.isValid() || !m_entity.hasComponent<ScriptListComponent>()) { return false; }
        std::vector<ExternalScript> &scripts = m_entity.getComponent<ScriptListComponent>().scripts;
        if (m_scriptIndex >= scripts.size()) { return false; }
        ExternalScript &script = scripts[m_scriptIndex];
        if (!script.hasField(m_fieldName.c_str())) { return false; }
        ScriptField &field = script.getField(m_fieldName.c_str());
        field.value = value;
        script.applyField(field);
        m_entity.setWorldChanged();
        return true;
    }

    Entity m_entity;
    size_t m_scriptIndex;
    std::string m_fieldName;
    ScriptFieldValue m_before;
    ScriptFieldValue m_after;
    std::chrono::steady_clock::time_point m_lastEdit;
    bool m_sealed = false;
};

} // namespace Panda
