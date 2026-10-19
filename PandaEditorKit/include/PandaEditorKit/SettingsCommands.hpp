#pragma once

#include "PandaEditorKit/EditorHost.hpp"

#include <Foundation/Foundation.hpp>
#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/WorldCommands/WorldCommand.hpp>

#include <chrono>
#include <utility>

namespace Editor {

// Settings edits from the sections (the settings scopes, context/plugins.md) — a single path for
// the core and the modules. A section form edits a copy of its Codable struct T and commits it;
// the host turns the command into an undo step of the world or of the project settings document,
// or applies the editor scope at once (EditorHost::commitSettings).

// The generic edit command of a section T in a store: the value before/after, undo/redo through
// assignment, merging of the ticks of one gesture (a drag = one undo).
template<typename T>
class SettingsUpdate final : public Panda::WorldCommand {
public:
    SettingsUpdate(Panda::TypeContext &store, T before, T after)
        : m_store(&store)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_lastEdit(std::chrono::steady_clock::now()) {}

    bool execute() override {
        m_store->get<T>() = m_after;
        return true;
    }
    bool undo() override {
        m_store->get<T>() = m_before;
        return true;
    }
    bool canMerge(Panda::WorldCommand &other) override {
        auto *update = dynamic_cast<SettingsUpdate *>(&other);
        return update && !m_sealed && update->m_store == m_store && Panda::withinMergeWindow(m_lastEdit);
    }
    void merge(Panda::WorldCommand &other) override {
        m_after = static_cast<SettingsUpdate &>(other).m_after;
        m_lastEdit = std::chrono::steady_clock::now();
    }
    void seal() override {
        m_sealed = true;
    }

private:
    Panda::TypeContext *m_store;
    T m_before;
    T m_after;
    std::chrono::steady_clock::time_point m_lastEdit;
    bool m_sealed = false;
};

// Write the value of section T of the scope as one undoable edit.
template<typename T>
void commitSettings(Panda::SettingsScope scope, const T &value) {
    Panda::TypeContext &store = editorHost().settings(scope);
    editorHost().commitSettings(scope, Foundation::makeShared<SettingsUpdate<T>>(store, store.get<T>(), value));
}

} // namespace Editor
