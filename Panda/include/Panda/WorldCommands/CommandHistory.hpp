#pragma once

#include "WorldCommand.hpp"

#include <Foundation/Foundation.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <type_traits>
#include <utility>

namespace Panda {

// The undo/redo history of ONE document (a world — a member of World; an asset — the map of
// AssetDocuments; the settings of the project — their document). A step is a command; a command
// that continues the gesture of the step before it is merged into that step (WorldCommand::canMerge).
class CommandHistory final {
public:
    // Runs the command and makes it the last step of the history.
    template<typename Command>
        requires std::is_base_of_v<WorldCommand, Command>
    void execute(Command &command) {
        add(Foundation::makeShared<Command>(std::move(command)), true);
    }
    // The same for a command the caller built with its type erased (the settings commands).
    void execute(Foundation::Shared<WorldCommand> command) {
        add(std::move(command), true);
    }

    // Makes a change that is already applied a step of the history: the command is there to be
    // undone (an entity that was just created, a stroke of a brush, a drag of the gizmo).
    template<typename Command>
        requires std::is_base_of_v<WorldCommand, Command>
    void record(Command &command) {
        add(Foundation::makeShared<Command>(std::move(command)), false);
    }

    // Grows with every step made, undone or redone: who shows something derived from the document
    // (what an instance of a prefab differs in) looks again when it is another number.
    uint64_t revision() const {
        return m_revision;
    }

    bool canUndo() const {
        return !m_undoStack.empty();
    }

    void undo() {
        if (!canUndo()) { return; }
        auto command = m_undoStack.back();
        m_undoStack.pop_back();
        m_revision++;
        if (!command->undo()) {
            failed();
            return;
        }
        m_redoStack.push_back(command);
        // The new top is a completed history step: future edits are not merged into it.
        if (!m_undoStack.empty()) { m_undoStack.back()->seal(); }
    }

    bool canRedo() const {
        return !m_redoStack.empty();
    }

    void redo() {
        if (!canRedo()) { return; }
        auto command = m_redoStack.back();
        m_redoStack.pop_back();
        m_revision++;
        if (!command->execute()) {
            failed();
            return;
        }
        m_undoStack.push_back(command);
        command->seal();
    }

    void clear() {
        m_undoStack.clear();
        m_redoStack.clear();
        m_revision++;
    }

private:
    static constexpr size_t MAX_STEPS = 50;

    void add(Foundation::Shared<WorldCommand> command, bool run) {
        m_revision++;
        WorldCommand *last = m_undoStack.empty() ? nullptr : m_undoStack.back().get();
        if (last && last->canMerge(*command)) {
            last->merge(*command);
        } else {
            m_undoStack.push_back(std::move(command));
        }
        if (run && !m_undoStack.back()->execute()) {
            failed();
            return;
        }
        if (m_undoStack.size() > MAX_STEPS) { m_undoStack.pop_front(); }
        m_redoStack.clear();
    }

    // A command that could not be executed or undone leaves the document in a state the history
    // no longer describes: the history is dropped.
    void failed() {
        LOG_ERROR("A command of the undo history failed: the history is cleared.");
        clear();
    }

    std::deque<Foundation::Shared<WorldCommand>> m_undoStack;
    std::deque<Foundation::Shared<WorldCommand>> m_redoStack;
    uint64_t m_revision = 0;
};

} // namespace Panda
