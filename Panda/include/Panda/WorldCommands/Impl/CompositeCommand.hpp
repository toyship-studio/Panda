#pragma once

#include "Panda/WorldCommands/WorldCommand.hpp"

#include <Foundation/Foundation.hpp>

#include <type_traits>
#include <utility>
#include <vector>

namespace Panda {

// Several commands as one step of the history: executed in the order they were added, undone the
// other way round. A Revert of a whole instance of a prefab takes its additions out and brings
// its entities to the prefab in one step.
class CompositeCommand final : public WorldCommand {
public:
    template<typename Command>
        requires std::is_base_of_v<WorldCommand, std::decay_t<Command>>
    void add(Command &&command) {
        m_steps.push_back(Foundation::makeShared<std::decay_t<Command>>(std::forward<Command>(command)));
    }

    bool execute() override {
        for (const Foundation::Shared<WorldCommand> &step : m_steps) {
            if (!step->execute()) { return false; }
        }
        return !m_steps.empty();
    }

    bool undo() override {
        for (auto step = m_steps.rbegin(); step != m_steps.rend(); ++step) {
            if (!(*step)->undo()) { return false; }
        }
        return !m_steps.empty();
    }

private:
    std::vector<Foundation::Shared<WorldCommand>> m_steps;
};

} // namespace Panda
