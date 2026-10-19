//
// Created by Admin on 12.02.2022.
//

#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"

namespace Panda {

// Asset registry wrapper over RuntimeMesh — for geometry referenced through AssetRef
// (script meshes, sky and other primitives). The internal meshes of systems (chunks) own
// a RuntimeMesh directly, without an asset.
class RuntimeMeshAsset : public Asset {
public:
    RuntimeMeshAsset()
        : Asset(AssetType::RUNTIME_MESH) {}

    void update(const MeshData &data) {
        m_mesh.update(data);
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
};

} // namespace Panda
