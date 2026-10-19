#include "Panda/Assets/ImportedMeshAsset.hpp"
#include <cfloat>
#include "Panda/Assets/StaticResources.hpp"

namespace Panda {

ImportedMeshAsset::~ImportedMeshAsset() {
    for (auto &submesh : m_submeshes) {
        for (auto &primitive : submesh.primitives) {
            if (primitive.vertexBufferHandle.isValid()) { Miren::deleteVertexBuffer(primitive.vertexBufferHandle); }
            if (primitive.indexBufferHandle.isValid()) { Miren::deleteIndexBuffer(primitive.indexBufferHandle); }
        }
    }
}

ImportedMeshAsset::ImportedMeshAsset(std::vector<ImportedMeshData> meshes)
    : Asset(AssetType::IMPORTED_MESH)
    , m_submeshes() {
    for (const auto &meshData : meshes) {
        ImportedSubmesh &submesh = m_submeshes.emplace_back();
        for (const auto &primitiveData : meshData.primitives) {
            ImportedPrimitive &primitive = submesh.primitives.emplace_back();
            if (primitiveData.vertices.empty()) { continue; }
            glm::vec3 minPoint(FLT_MAX);
            glm::vec3 maxPoint(-FLT_MAX);
            for (const Vertex &vertex : primitiveData.vertices) {
                const glm::vec3 position(vertex.pos.x, vertex.pos.y, vertex.pos.z);
                minPoint = glm::min(minPoint, position);
                maxPoint = glm::max(maxPoint, position);
            }
            primitive.boundsMin = minPoint;
            primitive.boundsMax = maxPoint;
            primitive.hasBounds = true;
            primitive.indicesCount = primitiveData.indices.size();
            primitive.bufferLayoutHandle = StaticResources::generalVertexLayout;
            size_t verticesSize = primitiveData.vertices.size() * sizeof(Vertex);
            Foundation::Memory vertices = Foundation::Memory::copying(primitiveData.vertices.data(), verticesSize);
            primitive.vertexBufferHandle =
                Miren::createVertexBuffer(vertices, verticesSize, primitive.bufferLayoutHandle);
            Foundation::Memory indices = Foundation::Memory::copying(
                primitiveData.indices.data(), primitiveData.indices.size() * sizeof(uint32_t)
            );
            primitive.indexBufferHandle =
                Miren::createIndexBuffer(indices, Miren::BufferElementType::UnsignedInt, primitiveData.indices.size());
        }
    }
}

} // namespace Panda
