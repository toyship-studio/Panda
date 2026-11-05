//
// Created by Admin on 12.02.2022.
//

#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Renderer/MeshGeometry.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"

namespace Panda {

// Asset registry wrapper over RuntimeMesh — for geometry referenced through AssetRef
// (script meshes, sky and other primitives). The internal meshes of systems (chunks) own
// a RuntimeMesh directly, without an asset. The asset keeps the triangles on the CPU too
// (MeshGeometry): what a reference can name, a mesh collider can be built from.
class RuntimeMeshAsset : public Asset {
public:
    RuntimeMeshAsset()
        : Asset(AssetType::RUNTIME_MESH) {}

    void update(const MeshData &data) {
        m_geometry = MeshGeometry::of(data);
        m_mesh.update(data);
    }
    // The triangles of the last update; empty when its vertex stride was unknown.
    const MeshGeometry &getGeometry() const {
        return m_geometry;
    }
    bool isValid() {
        return m_mesh.isValid();
    }
    bool getLocalBounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
        return m_mesh.getLocalBounds(outMin, outMax);
    }
    const RuntimeMesh &getMesh() const {
        return m_mesh;
    }

private:
    RuntimeMesh m_mesh;
    MeshGeometry m_geometry;
};

} // namespace Panda
