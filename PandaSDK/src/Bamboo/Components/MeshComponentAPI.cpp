#include "Bamboo/Components/MeshComponentAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace MeshComponentAPI {
    MeshHandle getMesh(EntityHandle entity) {
        if (!Internal::requireMainThreadApi("MeshComponentAPI::getMesh")) { return {}; }
        Panda::Handle result;
        Panda::ExternalCalls::meshComponent_GetMesh(entity.id, &result);
        return MeshHandle(result);
    }

    void setMesh(EntityHandle entity, MeshHandle mesh) {
        if (!Internal::requireMainThreadApi("MeshComponentAPI::setMesh")) { return; }
        Panda::ExternalCalls::meshComponent_SetMesh(entity.id, mesh.id);
    }

    void setMaterial(EntityHandle entity, MaterialHandle material) {
        if (!Internal::requireMainThreadApi("MeshComponentAPI::setMaterial")) { return; }
        Panda::ExternalCalls::meshComponent_SetMaterial(entity.id, material.id);
    }

} // namespace MeshComponentAPI

} // namespace Bamboo