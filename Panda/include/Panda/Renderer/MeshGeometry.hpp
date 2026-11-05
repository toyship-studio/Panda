#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Panda {

struct MeshData;

// The triangles of a mesh on the CPU: the positions and the indices, three per triangle. The mesh
// assets keep it next to their GPU buffers for whoever needs the geometry besides the renderer
// (the mesh collider of the physics builds its hull or its triangle mesh from it).
struct MeshGeometry {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;

    bool empty() const {
        return positions.size() < 3 || indices.size() < 3;
    }
    // Appends another geometry: its indices are offset by the positions already here.
    void append(const MeshGeometry &other);
    // The positions and the indices of mesh data whose vertex stride is known (the position is
    // the first three floats of a vertex, the engine convention); empty when the stride is not.
    static MeshGeometry of(const MeshData &data);
};

} // namespace Panda
