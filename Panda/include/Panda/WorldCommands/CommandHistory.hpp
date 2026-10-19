#pragma once

#include "WorldCommand.hpp"

#include <Foundation/Foundation.hpp>

#include <deque>
#include <type_traits>
#include <utility>

namespace Panda {

// Undo/redo history of ONE document (world — a World member; assets — a map in AssetHandlerEditor).
class CommandHistory final {
    inline static int MAX_OPERATIONS_IN_CACHE = 50;

public:
    // Do not prefill the deques: deque(N) is N null commands (size, not capacity), CAN_UNDO
    // of an empty stack became true and UNDO crashed on null. The cap is kept by SAVE (pop_front).
    CommandHistory() = default;

    /// Save new command to history and execute if it needs to.
    template<typename CMD>
    void SAVE(CMD &cmd, bool needToExecute = true) {
        static_assert(std::is_base_of_v<WorldCommand, CMD>, "Not inherited from WorldCommand");
        SAVE_SHARED(Foundation::makeShared<CMD>(std::move(cmd)), needToExecute);
    }

    /// Save a ready command (built by the caller, the type is erased: the settings commands of the
    /// editor host) and execute if it needs to.
    void SAVE_SHARED(Foundation::Shared<WorldCommand> command, bool needToExecute = true) {
        auto last = PREVIOUS_COMMAND();
        if (last && last->canMerge(*command)) {
            last->merge(*command);
        } else {
            undoStack.push_back(std::move(command));
        }
        if (needToExecute) {
            last = PREVIOUS_COMMAND();
            if (!last->execute()) {
                LOG_ERROR("ERROR EXECUTING COMMAND");
                CLEAR();
            }
        }
        if (undoStack.size() > MAX_OPERATIONS_IN_CACHE) { undoStack.pop_front(); }
        redoStack.clear();
    }

    bool CAN_UNDO() {
        return !undoStack.empty();
    }

    void UNDO() {
        if (!CAN_UNDO()) { return; }
        auto command = undoStack.back();
        undoStack.pop_back();
        if (!command->undo()) {
            LOG_ERROR("ERROR EXECUTING COMMAND");
            CLEAR();
            return;
        }
        redoStack.push_back(command);
        // The new top is a completed history step: future edits are not merged into it.
        if (!undoStack.empty()) { undoStack.back()->seal(); }
    }

    bool CAN_REDO() {
        return !redoStack.empty();
    }

    void REDO() {
        if (!CAN_REDO()) { return; }
        auto command = redoStack.back();
        redoStack.pop_back();
        if (!command->execute()) {
            LOG_ERROR("ERROR EXECUTING COMMAND");
            CLEAR();
            return;
        }
        undoStack.push_back(command);
        command->seal();
    }

    Foundation::Shared<WorldCommand> PREVIOUS_COMMAND() {
        if (undoStack.empty()) { return nullptr; }
        return undoStack.back();
    }

    void CLEAR() {
        undoStack.clear();
        redoStack.clear();
    }

private:
    std::deque<Foundation::Shared<WorldCommand>> undoStack;
    std::deque<Foundation::Shared<WorldCommand>> redoStack;
};

} // namespace Panda
