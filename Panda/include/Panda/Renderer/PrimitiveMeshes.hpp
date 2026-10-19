#pragma once

#include "Panda/Renderer/MeshData.hpp"

namespace Panda {

// Built-in primitives (greyboxing): unit dimensions, sizes come from the transform scale.
// Format — the general Vertex with proper normals and UVs; vertexStride is set, so the
// AABB for view frustum culling is computed automatically.
namespace PrimitiveMeshes {

    MeshData buildCube();     // 1×1×1, centered at the origin
    MeshData buildSphere();   // diameter 1
    MeshData buildCapsule();  // radius 0.5, full height 2 (Y axis) — character dimensions
    MeshData buildPlane();    // 1×1 in the XZ plane, normal +Y
    MeshData buildCylinder(); // diameter 1, height 1 (Y axis)

} // namespace PrimitiveMeshes

} // namespace Panda
