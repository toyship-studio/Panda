#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"

#include <Miren/Base.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Panda {

class Renderer3D;

// A thing a system draws many times without making an entity of each copy: the trees of a
// terrain, the particles of an emitter. It is the meshes of an imported model, or of a prefab —
// every mesh the entities of the prefab draw, each where it stands under the root — and it is
// drawn with streams of instance matrices: one draw call per mesh, whatever the number of copies.
//
// A mesh of the model stands at a `place` in it. The system keeps one stream of matrices per
// place — where a copy stands × the place — so the meshes that share a place (every primitive
// of an imported model does) share a stream.
//
// What else a prefab holds — lights, scripts, colliders — is not a part of the picture: a system
// that wants more of a prefab reads its template itself (the terrain gives the colliders of a
// tree prefab to the physics).
class InstancedModel final {
public:
    struct Part {
        uint32_t place = 0; // index into places()
        // The material the entity of the prefab names itself, or the one given for a whole
        // imported model; empty — the material of the slot of the imported mesh, as it is at the
        // moment of the draw, and without one the instanced lit material of the engine.
        AssetRef<MaterialAsset> material;
        AssetRef<ImportedMeshAsset> imported; // a primitive of an imported mesh…
        uint32_t submeshIndex = 0;
        uint32_t primitiveIndex = 0;
        AssetRef<RuntimeMeshAsset> mesh; // …or a mesh of the registry (a primitive shape)
    };

    // What a part is drawn from at this moment: the buffers of its mesh and its material — the
    // part's own, or the one of the slot of the imported mesh; none — the one who draws chooses.
    struct Draw {
        Miren::VertexBufferHandle vertexBuffer{MIREN_INVALID_HANDLE};
        Miren::IndexBufferHandle indexBuffer{MIREN_INVALID_HANDLE};
        Miren::VertexLayoutHandle vertexLayout{MIREN_INVALID_HANDLE};
        uint32_t indicesCount = 0;
        AssetRef<MaterialAsset> material;
    };
    // false — the mesh of the part is gone, or holds nothing to draw.
    static bool resolve(const Part &part, Draw &out);

    InstancedModel() = default;
    // An imported model as it is: every primitive, with the material of its slot — or with
    // `material`, when it is given, for all of them. Empty while the mesh is not loaded.
    static InstancedModel ofMesh(const AssetRef<Asset> &mesh, const AssetRef<Asset> &material);
    // The meshes of a prefab. Where the root of the prefab stands and how it is turned are left
    // out — a copy stands where the system puts it; its scale stays. Empty while the prefab is
    // not loaded, and when it draws nothing.
    static InstancedModel ofPrefab(const AssetRef<Asset> &prefab);

    bool empty() const {
        return m_parts.empty();
    }
    const std::vector<Part> &parts() const {
        return m_parts;
    }
    const std::vector<glm::mat4> &places() const {
        return m_places;
    }
    // The box around the model in its own space; false — nothing of it has bounds.
    bool bounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
        outMin = m_boundsMin;
        outMax = m_boundsMax;
        return m_bounded;
    }
    // What the model was made of, to compare: another source — or the same prefab loaded again —
    // is another stamp. 0 — there is no source: none is named, or it is not loaded yet.
    uint64_t stamp() const {
        return m_stamp;
    }
    // The stamp a model of the source would have now, without making it: a system that holds a
    // model asks whether its source is still the one.
    static uint64_t stampOfMesh(const AssetRef<Asset> &mesh, const AssetRef<Asset> &material);
    static uint64_t stampOfPrefab(const AssetRef<Asset> &prefab);
    // The meshes that stand at the place, each as `count` instances of the stream (matrices,
    // StaticResources::instanceMat4Layout; the final transform is model × instance). The scene
    // and the shadow depth passes; instances are not picked.
    void submit(
        Renderer3D &renderer, glm::mat4 &model, uint32_t place, Miren::VertexBufferHandle instances, uint32_t count
    ) const;

private:
    uint32_t placeIndex(const glm::mat4 &place);
    void include(const glm::mat4 &place, const glm::vec3 &localMin, const glm::vec3 &localMax);

    std::vector<Part> m_parts;
    std::vector<glm::mat4> m_places;
    glm::vec3 m_boundsMin{0.f};
    glm::vec3 m_boundsMax{0.f};
    bool m_bounded = false;
    uint64_t m_stamp = 0;
};

} // namespace Panda
