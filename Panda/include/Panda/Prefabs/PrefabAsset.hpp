#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/GameLogic/World.hpp"

namespace Panda {

// A prefab as an asset: the entities of its file in a world of their own — the template its
// instances are copies of (World::instantiate). The prefab is expanded there: an instance of
// another prefab nested in it is entities like the rest, marked with where they came from
// (PrefabComponent). The world never runs and is never drawn; who made the asset filled it —
// the editor from the `.prefab` file (Prefabs::load), a game from the cooked one.
class PrefabAsset final : public Asset {
public:
    PrefabAsset()
        : Asset(AssetType::PREFAB) {}

    World &world() {
        return m_world;
    }
    // A number no other loaded prefab has, this one loaded again included: what was built from
    // the template (the meshes a system draws, the shapes of its colliders) is stale when the
    // serial it was built for is not the one of the asset any more.
    uint64_t serial() const {
        return m_serial;
    }
    // The root of the prefab: its one entity without a parent. Invalid — the prefab holds no
    // entity (its file is missing or empty).
    Entity root() {
        Entity root = m_world.getById(m_root);
        if (root.isValid()) { return root; }
        const std::vector<UUID> roots = m_world.orderedChildren(UUID(0));
        m_root = roots.empty() ? UUID(0) : roots.front();
        return m_world.getById(m_root);
    }

private:
    static uint64_t nextSerial() {
        static uint64_t last = 0;
        return ++last;
    }

    World m_world;
    UUID m_root = 0;
    uint64_t m_serial = nextSerial();
};

} // namespace Panda
