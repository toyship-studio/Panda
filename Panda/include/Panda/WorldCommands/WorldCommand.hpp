#pragma once

#include <chrono>
#include <cstdint>

namespace Panda {

class WorldCommand {
public:
    virtual ~WorldCommand() = default;
    virtual bool execute() = 0;
    virtual bool undo() = 0;
    virtual void merge(WorldCommand &command) {}
    virtual bool canMerge(WorldCommand &command) {
        return false;
    }
    // Breaks a merge series: a continuation of the gesture after undo/redo must not be
    // merged into a rolled-back/foreign command. Default — no-op (commands without canMerge
    // are unaffected).
    virtual void seal() {}
    // TODO: Store selection context for each command
};

// The window of a merge series: a command that fuses the ticks of a continuous gesture into one
// history step (canMerge/merge) merges the next tick only while the previous one is this recent —
// two separate edits of the same value stay two steps.
inline bool withinMergeWindow(std::chrono::steady_clock::time_point last) {
    constexpr auto MERGE_WINDOW = std::chrono::milliseconds(1000);
    return std::chrono::steady_clock::now() - last <= MERGE_WINDOW;
}

} // namespace Panda
