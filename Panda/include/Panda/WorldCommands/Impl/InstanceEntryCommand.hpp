#pragma once

#ifdef PND_EDITOR

#    include "Panda/GameLogic/World.hpp"
#    include "Panda/Prefabs/Prefabs.hpp"
#    include "Panda/WorldCommands/WorldCommand.hpp"

#    include <string>
#    include <utility>

namespace Panda {

// An instance of a prefab becomes what an entry says (Prefabs::setEntry) — the entry of a world
// file: the prefab and what the instance differs in. Revert in all its sizes is this command:
// the entry the instance had, and the entry without a part of the difference. The entries are
// text and the instance is found by the id of its root, so the command holds no entity.
class InstanceEntryCommand : public WorldCommand {
public:
    InstanceEntryCommand(World &world, UUID root, std::string before, std::string after)
        : m_world(&world)
        , m_root(root)
        , m_before(std::move(before))
        , m_after(std::move(after)) {}

    bool execute() override {
        return set(m_after);
    }
    bool undo() override {
        return set(m_before);
    }

private:
    bool set(const std::string &entry) {
        Entity root = m_world->getById(m_root);
        if (!root.isValid()) { return false; }
        Prefabs::setEntry(root, entry);
        m_world->setChanged();
        return true;
    }

    World *m_world;
    UUID m_root;
    std::string m_before;
    std::string m_after;
};

} // namespace Panda

#endif // PND_EDITOR
