#include "Panda/Renderer/InstancedModel.hpp"

#include "Panda/Assets/StaticResources.hpp"
#include "Panda/GameLogic/Systems/RenderSystem.hpp"
#include "Panda/Prefabs/PrefabAsset.hpp"
#include "Panda/Renderer/Renderer3D.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Panda {

namespace {

    // The kinds of a source in a stamp: a mesh and a prefab never give the same one.
    constexpr uint64_t MESH_STAMP = 1;
    constexpr uint64_t PREFAB_STAMP = 2;

    uint64_t mix(uint64_t x) {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }

    uint64_t stampOf(uint64_t kind, uint64_t a, uint64_t b) {
        const uint64_t stamp = mix(mix(mix(kind) ^ a) ^ b);
        return stamp != 0 ? stamp : 1;
    }

    bool same(const glm::mat4 &l, const glm::mat4 &r) {
        for (int column = 0; column < 4; column++) {
            for (int row = 0; row < 4; row++) {
                if (std::abs(l[column][row] - r[column][row]) > 1.e-5f) { return false; }
            }
        }
        return true;
    }

} // namespace

bool InstancedModel::resolve(const Part &part, Draw &out) {
    out.material = part.material;
    if (RuntimeMeshAsset *asset = part.mesh.get()) {
        const RuntimeMesh &mesh = asset->getMesh();
        out.vertexBuffer = mesh.m_vertexBufferHandle;
        out.indexBuffer = mesh.m_indexBufferHandle;
        out.vertexLayout = mesh.m_bufferLayoutHandle;
        out.indicesCount = mesh.m_indicesCount;
        return out.vertexBuffer.isValid() && out.indexBuffer.isValid();
    }
    const ImportedMeshAsset *mesh = part.imported.get();
    if (mesh == nullptr || part.submeshIndex >= mesh->submeshCount()) { return false; }
    const ImportedSubmesh &submesh = mesh->submeshAt(part.submeshIndex);
    if (part.primitiveIndex >= submesh.primitives.size()) { return false; }
    const ImportedPrimitive &primitive = submesh.primitives[part.primitiveIndex];
    out.vertexBuffer = primitive.vertexBufferHandle;
    out.indexBuffer = primitive.indexBufferHandle;
    out.vertexLayout = primitive.bufferLayoutHandle;
    out.indicesCount = primitive.indicesCount;
    if (!out.material) { out.material = mesh->materialForPrimitive(part.submeshIndex, part.primitiveIndex).as<MaterialAsset>(); }
    return out.vertexBuffer.isValid() && out.indexBuffer.isValid();
}

uint32_t InstancedModel::placeIndex(const glm::mat4 &place) {
    for (size_t i = 0; i < m_places.size(); i++) {
        if (same(m_places[i], place)) { return static_cast<uint32_t>(i); }
    }
    m_places.push_back(place);
    return static_cast<uint32_t>(m_places.size() - 1);
}

void InstancedModel::include(const glm::mat4 &place, const glm::vec3 &localMin, const glm::vec3 &localMax) {
    // The box of the placed local box: its center and its extents through abs(M).
    const glm::vec3 center = glm::vec3(place * glm::vec4((localMin + localMax) * 0.5f, 1.f));
    const glm::vec3 half = (localMax - localMin) * 0.5f;
    const glm::mat3 axes(place);
    const glm::vec3 extents = glm::abs(axes[0]) * half.x + glm::abs(axes[1]) * half.y + glm::abs(axes[2]) * half.z;
    m_boundsMin = m_bounded ? glm::min(m_boundsMin, center - extents) : center - extents;
    m_boundsMax = m_bounded ? glm::max(m_boundsMax, center + extents) : center + extents;
    m_bounded = true;
}

uint64_t InstancedModel::stampOfMesh(const AssetRef<Asset> &mesh, const AssetRef<Asset> &material) {
    return mesh.as<ImportedMeshAsset>().get() != nullptr ? stampOf(MESH_STAMP, mesh.getId(), material.getId()) : 0;
}

uint64_t InstancedModel::stampOfPrefab(const AssetRef<Asset> &prefab) {
    const PrefabAsset *asset = prefab.as<PrefabAsset>().get();
    return asset != nullptr ? stampOf(PREFAB_STAMP, prefab.getId(), asset->serial()) : 0;
}

InstancedModel InstancedModel::ofMesh(const AssetRef<Asset> &meshRef, const AssetRef<Asset> &materialRef) {
    InstancedModel model;
    AssetRef<ImportedMeshAsset> mesh = meshRef.as<ImportedMeshAsset>();
    const ImportedMeshAsset *asset = mesh.get();
    if (asset == nullptr) { return model; }
    model.m_stamp = stampOfMesh(meshRef, materialRef);
    const AssetRef<MaterialAsset> material = materialRef.as<MaterialAsset>();
    const glm::mat4 identity(1.f);
    for (size_t submeshIndex = 0; submeshIndex < asset->submeshCount(); submeshIndex++) {
        const ImportedSubmesh &submesh = asset->submeshAt(submeshIndex);
        for (size_t primitiveIndex = 0; primitiveIndex < submesh.primitives.size(); primitiveIndex++) {
            const ImportedPrimitive &primitive = submesh.primitives[primitiveIndex];
            if (!primitive.vertexBufferHandle.isValid() || !primitive.indexBufferHandle.isValid()) { continue; }
            Part part;
            part.place = model.placeIndex(identity);
            part.material = material;
            part.imported = mesh;
            part.submeshIndex = static_cast<uint32_t>(submeshIndex);
            part.primitiveIndex = static_cast<uint32_t>(primitiveIndex);
            model.m_parts.push_back(std::move(part));
            if (primitive.hasBounds) { model.include(identity, primitive.boundsMin, primitive.boundsMax); }
        }
    }
    return model;
}

InstancedModel InstancedModel::ofPrefab(const AssetRef<Asset> &prefabRef) {
    InstancedModel model;
    PrefabAsset *prefab = prefabRef.as<PrefabAsset>().get();
    if (prefab == nullptr) { return model; }
    // A prefab that draws nothing is still a source that was read: the stamp says which.
    model.m_stamp = stampOfPrefab(prefabRef);
    Entity root = prefab->root();
    if (!root.isValid()) { return model; }
    // The meshes of the template the way a world lists its own for a frame: the same components
    // draw, with the same materials.
    RenderSystem::MeshList list;
    list.collect(prefab->world());
    // A copy stands where it is placed: the place and the turn of the root are not of the model.
    const TransformComponent &transform = root.getTransform();
    const glm::mat4 rootPose = glm::translate(glm::mat4(1.f), transform.getPosition()) * glm::mat4_cast(transform.getRotation());
    const glm::mat4 toModel = glm::inverse(rootPose);
    for (const RenderSystem::MeshList::Item &item : list.items()) {
        if (!item.imported && !item.mesh) { continue; }
        const glm::mat4 place = toModel * item.transform;
        Part part;
        part.place = model.placeIndex(place);
        // The material of a slot of an imported mesh is asked of the mesh when it is drawn: the
        // slot may get another one while the model lives.
        if (item.ownMaterial || !item.imported) { part.material = item.material; }
        part.imported = item.imported;
        part.submeshIndex = item.submeshIndex;
        part.primitiveIndex = item.primitiveIndex;
        part.mesh = item.mesh;
        model.m_parts.push_back(std::move(part));
        if (item.bounded) { model.include(place, item.boundsMin, item.boundsMax); }
    }
    return model;
}

void InstancedModel::submit(
    Renderer3D &renderer, glm::mat4 &model, uint32_t place, Miren::VertexBufferHandle instances, uint32_t count
) const {
    for (const Part &part : m_parts) {
        if (part.place != place) { continue; }
        renderer.submitInstanced(model, part, instances, StaticResources::instanceMat4Layout, count);
    }
}

} // namespace Panda
