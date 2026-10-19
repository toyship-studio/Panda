#pragma once

#include "Panda/GameLogic/Entity.hpp"

#include <unordered_map>

namespace Panda {

// The part of EntityBodies that does not depend on the kind of a body: the world is told of
// every body (World::trackEntity / untrackEntity keep its count of tracked entities by subtree,
// so a transform change above a body reaches it).
class EntityBodiesBase {
protected:
    static void bodyAdded(Entity entity);
    static void bodyRemoved(Entity entity);
};

// The bodies of a simulation by entity — the table Physics2D and Physics3D keep theirs in. A
// body gets onto an entity and off it only here, and the world is told every time, so no code of
// a simulation can put a body past the count the world keeps. Looking a body up never creates an
// entry.
template<typename TBody>
class EntityBodies final : private EntityBodiesBase {
public:
    void add(Entity entity, const TBody &body) {
        PND_ASSERT(!contains(entity.getId()), "THE ENTITY ALREADY HAS A BODY");
        if (m_entries.insert_or_assign(entity.getId(), Entry {entity, body}).second) { bodyAdded(entity); }
    }

    void remove(Entity entity) {
        if (m_entries.erase(entity.getId()) != 0) { bodyRemoved(entity); }
    }

    // The simulation is over: every body is gone.
    void clear() {
        for (auto &[entityId, entry] : m_entries) {
            bodyRemoved(entry.entity);
        }
        m_entries.clear();
    }

    // The body of the entity; nullptr — it has none.
    TBody *find(UUID entityId) {
        const auto it = m_entries.find(entityId);
        return it != m_entries.end() ? &it->second.body : nullptr;
    }

    bool contains(UUID entityId) const {
        return m_entries.contains(entityId);
    }

    // Every body, to read or to change in place.
    template<typename TVisitor>
    void forEach(TVisitor &&visit) {
        for (auto &[entityId, entry] : m_entries) {
            visit(entry.body);
        }
    }

private:
    struct Entry {
        Entity entity;
        TBody body;
    };

    std::unordered_map<UUID, Entry> m_entries;
};

} // namespace Panda
