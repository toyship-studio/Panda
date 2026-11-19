#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Base/UUID.hpp"

#include <cstdint>

namespace Panda {

// Where an entity came from: it is a part of an instance of a prefab (context/prefabs.md). A
// world holds the entities of an instance as ordinary entities; this is the only thing that
// tells them from the rest, and nothing that runs a world reads it — the editor does (what
// differs from the prefab, the color of the Hierarchy), and the writer of a world file, which
// turns the entities of an instance back into one entry.
struct PrefabComponent final {
    // The root entity of the instance this entity is a part of. On that root it is its own id.
    UUID instance = 0;
    // The entity of the prefab it was made from: its id in the expanded prefab (PrefabAsset).
    UUID source = 0;
    // On a root — of the instance, or of an instance of another prefab nested in it — the prefab
    // asset; 0 on the entities under it.
    AssetId prefab = 0;
};

// The key of the component in the expanded form of a world (WorldMapper).
constexpr const char *PREFAB_LINK_KEY = "prefabLink";
// An instance of a prefab the project does not have stands in a world as one entity that carries
// its entry of the file under this key, as it was read: no component type has the key, so the
// mapper keeps the value as it is and the writer of the file puts the entry back.
constexpr const char *PREFAB_MISSING_KEY = "missingPrefab";

// The id of an entity of an instance other than its root: a fixed mix of the id of the root and
// of the id the entity has in the prefab. It is the same wherever the instance is expanded — in
// the editor, in the cook, in a game that instantiates the prefab under a root id it chose — so
// a reference to an entity inside an instance is a number that holds. Never 0.
inline UUID derivedEntityId(UUID instance, UUID source) {
    uint64_t x = (static_cast<uint64_t>(static_cast<uint32_t>(instance)) << 32) | static_cast<uint32_t>(source);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    const uint32_t id = static_cast<uint32_t>(x);
    return UUID(id != 0 ? id : 1u);
}

// The derived id for a part when the first one is taken (two random 32-bit numbers met): the id
// mixed with the number of the attempt, until `taken` says it is free.
template<typename Taken>
UUID freeDerivedEntityId(UUID instance, UUID source, Taken taken) {
    UUID id = derivedEntityId(instance, source);
    for (uint32_t attempt = 1; taken(id); attempt++) { id = derivedEntityId(id, UUID(attempt)); }
    return id;
}

} // namespace Panda
