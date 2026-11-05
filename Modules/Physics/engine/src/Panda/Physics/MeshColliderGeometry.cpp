#include "Panda/Physics/MeshColliderGeometry.hpp"

#include <Panda/Assets/ImportedMeshAsset.hpp>
#include <Panda/Assets/RuntimeMeshAsset.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>

#include <box3d/box3d.h>

namespace Panda::Physics {

bool meshColliderSource(Entity entity, const MeshCollider3DComponent &collider, MeshColliderSource &outSource) {
    outSource = MeshColliderSource{};
    if (collider.mesh.getId() != 0) {
        outSource.asset = collider.mesh.getId();
        return true;
    }
    if (entity.hasComponent<ImportedSubmeshComponent>()) {
        const ImportedSubmeshComponent &submesh = entity.getComponent<ImportedSubmeshComponent>();
        outSource.asset = submesh.importedMesh.getId();
        outSource.submesh = submesh.submeshIndex;
        outSource.primitive = submesh.primitiveIndex;
        return outSource.asset != 0;
    }
    if (entity.hasComponent<RuntimeMeshComponent>()) {
        outSource.asset = entity.getComponent<RuntimeMeshComponent>().runtimeMesh.getId();
        return outSource.asset != 0;
    }
    if (entity.hasComponent<PrimitiveMeshComponent>()) {
        outSource.asset = StaticResources::primitiveMeshOf(entity.getComponent<PrimitiveMeshComponent>().kind).getId();
        return outSource.asset != 0;
    }
    return false;
}

bool meshColliderGeometry(const MeshColliderSource &source, MeshGeometry &out) {
    out = MeshGeometry{};
    if (source.asset == 0) { return false; }
    AssetRef<Asset> ref(source.asset);
    Asset *asset = ref.get();
    if (asset == nullptr) { return false; }
    if (asset->getType() == AssetType::IMPORTED_MESH) {
        const ImportedMeshAsset *imported = static_cast<const ImportedMeshAsset *>(asset);
        if (source.submesh == UINT32_MAX) {
            imported->appendGeometry(out);
        } else if (const MeshGeometry *primitive = imported->primitiveGeometry(source.submesh, source.primitive)) {
            out = *primitive;
        }
    } else if (asset->getType() == AssetType::RUNTIME_MESH) {
        out = static_cast<const RuntimeMeshAsset *>(asset)->getGeometry();
    }
    return !out.empty();
}

bool meshColliderGeometrySize(const MeshColliderSource &source, size_t &outPositions, size_t &outIndices) {
    outPositions = 0;
    outIndices = 0;
    if (source.asset == 0) { return false; }
    AssetRef<Asset> ref(source.asset);
    Asset *asset = ref.get();
    if (asset == nullptr) { return false; }
    if (asset->getType() == AssetType::IMPORTED_MESH) {
        const ImportedMeshAsset *imported = static_cast<const ImportedMeshAsset *>(asset);
        if (source.submesh == UINT32_MAX) {
            for (size_t submesh = 0; submesh < imported->submeshCount(); submesh++) {
                for (const ImportedPrimitive &primitive : imported->submeshAt(submesh).primitives) {
                    outPositions += primitive.geometry.positions.size();
                    outIndices += primitive.geometry.indices.size();
                }
            }
        } else if (const MeshGeometry *primitive = imported->primitiveGeometry(source.submesh, source.primitive)) {
            outPositions = primitive->positions.size();
            outIndices = primitive->indices.size();
        }
    } else if (asset->getType() == AssetType::RUNTIME_MESH) {
        const MeshGeometry &geometry = static_cast<const RuntimeMeshAsset *>(asset)->getGeometry();
        outPositions = geometry.positions.size();
        outIndices = geometry.indices.size();
    }
    return outPositions >= 3 && outIndices >= 3;
}

bool meshColliderHullOutline(
    const MeshGeometry &geometry, std::vector<glm::vec3> &outPoints,
    std::vector<std::pair<uint32_t, uint32_t>> &outEdges
) {
    outPoints.clear();
    outEdges.clear();
    if (geometry.positions.size() < 4) { return false; }
    std::vector<b3Vec3> points(geometry.positions.size());
    for (size_t i = 0; i < points.size(); i++) {
        points[i] = {geometry.positions[i].x, geometry.positions[i].y, geometry.positions[i].z};
    }
    // The same hull the simulation builds: box3d keeps at most 255 vertices and simplifies.
    b3HullData *hull = b3CreateHull(points.data(), static_cast<int>(points.size()), UINT8_MAX);
    if (hull == nullptr) { return false; }
    const b3Vec3 *hullPoints = b3GetHullPoints(hull);
    outPoints.reserve(hull->vertexCount);
    for (int i = 0; i < hull->vertexCount; i++) {
        outPoints.emplace_back(hullPoints[i].x, hullPoints[i].y, hullPoints[i].z);
    }
    // Half-edges come in twin pairs: one segment per pair.
    const b3HullHalfEdge *edges = b3GetHullEdges(hull);
    for (int i = 0; i < hull->edgeCount; i++) {
        if (i < edges[i].twin) { outEdges.emplace_back(edges[i].origin, edges[edges[i].twin].origin); }
    }
    b3DestroyHull(hull);
    return !outEdges.empty();
}

} // namespace Panda::Physics
