#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Renderer/ImportedMeshData.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

struct ImportedPrimitive {
    Miren::VertexLayoutHandle bufferLayoutHandle;
    Miren::IndexBufferHandle indexBufferHandle;
    Miren::VertexBufferHandle vertexBufferHandle;
    uint32_t indicesCount;
    // Local AABB of the primitive (from vertices at load time) — for frustum culling.
    glm::vec3 boundsMin{0.f};
    glm::vec3 boundsMax{0.f};
    bool hasBounds = false;
};

struct ImportedSubmesh {
    std::vector<ImportedPrimitive> primitives;
};

// Imported 3d model mesh
class ImportedMeshAsset : public Asset {
public:
    ~ImportedMeshAsset();
    ImportedMeshAsset(std::vector<ImportedMeshData> meshes);

    bool getPrimitiveBounds(
        size_t submesh, size_t primitive, glm::vec3 &outMin, glm::vec3 &outMax
    ) const {
        if (submesh >= m_submeshes.size()) { return false; }
        if (primitive >= m_submeshes[submesh].primitives.size()) { return false; }
        const ImportedPrimitive &data = m_submeshes[submesh].primitives[primitive];
        outMin = data.boundsMin;
        outMax = data.boundsMax;
        return data.hasBounds;
    }

    // Read access to the primitive structure (baking impostor atlases, etc.).
    size_t submeshCount() const {
        return m_submeshes.size();
    }
    const ImportedSubmesh &submeshAt(size_t index) const {
        return m_submeshes[index];
    }

    // --- Material layout (mesh document: edited in a tab, written to the sidecar on Save).
    // Slot = import material, primitives share slots; the flat per-primitive list
    // is kept built — the renderer reads it directly (it is shared by all mesh instances).
    void setupMaterials(std::vector<AssetRef<Asset>> slotMaterials, std::vector<int32_t> primitiveSlots) {
        m_slotMaterials = std::move(slotMaterials);
        m_primitiveSlots = std::move(primitiveSlots);
        rebuildPrimitiveMaterials();
    }
    void setSlotMaterial(size_t slot, AssetRef<Asset> material) {
        if (slot >= m_slotMaterials.size()) { return; }
        m_slotMaterials[slot] = std::move(material);
        rebuildPrimitiveMaterials();
        m_isChanged = true;
    }
    const std::vector<AssetRef<Asset>> &getSlotMaterials() const {
        return m_slotMaterials;
    }
    const std::vector<AssetRef<Asset>> &getPrimitiveMaterials() const {
        return m_primitiveMaterials;
    }
    // Primitive material by position (flat order: submeshes -> primitives, empty ones count).
    AssetRef<Asset> materialForPrimitive(size_t submesh, size_t primitive) const {
        size_t flat = primitive;
        for (size_t i = 0; i < submesh && i < m_submeshes.size(); i++) {
            flat += m_submeshes[i].primitives.size();
        }
        return flat < m_primitiveMaterials.size() ? m_primitiveMaterials[flat] : AssetRef<Asset>();
    }
    void setChanged(bool value) override {
        m_isChanged = value;
    }
    bool isChanged() const override {
        return m_isChanged;
    }

    // Overall AABB of the mesh (union of the primitives of all submeshes); false — no bounds.
    bool getMeshBounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
        bool any = false;
        for (const ImportedSubmesh &submesh : m_submeshes) {
            for (const ImportedPrimitive &primitive : submesh.primitives) {
                if (!primitive.hasBounds) { continue; }
                if (!any) {
                    outMin = primitive.boundsMin;
                    outMax = primitive.boundsMax;
                    any = true;
                } else {
                    outMin = glm::min(outMin, primitive.boundsMin);
                    outMax = glm::max(outMax, primitive.boundsMax);
                }
            }
        }
        return any;
    }

private:
    void rebuildPrimitiveMaterials() {
        m_primitiveMaterials.assign(m_primitiveSlots.size(), AssetRef<Asset>());
        for (size_t i = 0; i < m_primitiveSlots.size(); i++) {
            const int32_t slot = m_primitiveSlots[i];
            if (slot >= 0 && slot < static_cast<int32_t>(m_slotMaterials.size())) {
                m_primitiveMaterials[i] = m_slotMaterials[slot];
            }
        }
    }

    std::vector<ImportedSubmesh> m_submeshes;
    std::vector<AssetRef<Asset>> m_slotMaterials;      // slot = import material
    std::vector<int32_t> m_primitiveSlots;             // flat primitive -> slot (-1 = none)
    std::vector<AssetRef<Asset>> m_primitiveMaterials; // flat, for rendering
    bool m_isChanged = false;
    friend class Renderer3D;
};

} // namespace Panda
