#pragma once

#include "Panda/Physics/PhysicsComponents.hpp"

#include <Panda/GameLogic/Entity.hpp>
#include <Panda/Renderer/MeshGeometry.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace Panda::Physics {

// Where the triangles of a mesh collider come from: the mesh the component names, or the mesh the
// entity renders — the primitive of its ImportedSubmesh, the asset of its RuntimeMesh or of its
// PrimitiveMesh. Two colliders with the same source are built from the same triangles.
struct MeshColliderSource {
    AssetId asset = 0;
    uint32_t submesh = UINT32_MAX; // a primitive of an imported mesh; UINT32_MAX — the whole asset
    uint32_t primitive = UINT32_MAX;

    bool operator==(const MeshColliderSource &other) const = default;
    bool operator<(const MeshColliderSource &other) const {
        if (asset != other.asset) { return asset < other.asset; }
        if (submesh != other.submesh) { return submesh < other.submesh; }
        return primitive < other.primitive;
    }
};

// The source of the collider's triangles; false — the component names no mesh and the entity
// renders none.
bool meshColliderSource(Entity entity, const MeshCollider3DComponent &collider, MeshColliderSource &outSource);

// The triangles of the source, read now (an unloaded asset is loaded); false — the asset is not
// a mesh, is missing or has no triangles (out is empty then).
bool meshColliderGeometry(const MeshColliderSource &source, MeshGeometry &out);
// The size of the same triangles without copying them: a cheap check that a copy taken earlier
// still matches (a runtime mesh a script rewrote). false — no triangles.
bool meshColliderGeometrySize(const MeshColliderSource &source, size_t &outPositions, size_t &outIndices);

// The convex hull of the triangles as the collider takes it, for drawing: its points and its
// edges (pairs of point indices). false — the points give no hull (fewer than four off a plane).
bool meshColliderHullOutline(
    const MeshGeometry &geometry, std::vector<glm::vec3> &outPoints,
    std::vector<std::pair<uint32_t, uint32_t>> &outEdges
);

} // namespace Panda::Physics
