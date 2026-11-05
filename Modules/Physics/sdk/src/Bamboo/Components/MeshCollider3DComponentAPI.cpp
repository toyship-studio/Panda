#include "Bamboo/Components/MeshCollider3DComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace MeshCollider3DComponentAPI {
    void setMesh(EntityHandle entity, MeshHandle mesh, bool convex) {
        if (!Internal::requireMainThreadApi("MeshCollider3DComponentAPI::setMesh")) { return; }
        if (const auto *api = Internal::physicsTable()) {
            api->meshCollider3DComponent_SetMesh(entity.id, mesh.id, convex ? 1u : 0u);
        }
    }

    MeshHandle getMesh(EntityHandle entity) {
        if (!Internal::requireMainThreadApi("MeshCollider3DComponentAPI::getMesh")) { return MeshHandle(); }
        Panda::Handle mesh = BAMBOO_INVALID_HANDLE;
        if (const auto *api = Internal::physicsTable()) { api->meshCollider3DComponent_GetMesh(entity.id, &mesh); }
        return MeshHandle(mesh);
    }
} // namespace MeshCollider3DComponentAPI

} // namespace Bamboo
