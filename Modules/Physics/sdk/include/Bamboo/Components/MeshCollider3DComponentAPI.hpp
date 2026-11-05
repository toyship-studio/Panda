#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// The mesh of a Mesh Collider 3D (the component is added as "meshCollider3dComponent"). The
// collider takes a runtime mesh of the game (AssetManagerAPI::createMesh + MeshAPI::update) or,
// with an invalid handle, the mesh the entity itself renders. convex — the convex hull of the
// mesh, the shape a dynamic or kinematic body takes; false — the triangles themselves, static
// bodies only. The geometry is read at the call: after another MeshAPI::update of the same mesh
// call setMesh again. While the kind of the shape stays, the body keeps its pose, velocities and
// joints. Calls on an entity without the component do nothing.
namespace MeshCollider3DComponentAPI {
    void setMesh(EntityHandle entity, MeshHandle mesh, bool convex);
    MeshHandle getMesh(EntityHandle entity); // the mesh the component names; invalid — the entity's own
} // namespace MeshCollider3DComponentAPI

} // namespace Bamboo
