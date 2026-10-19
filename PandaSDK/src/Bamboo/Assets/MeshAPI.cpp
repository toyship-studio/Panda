#include "Bamboo/Assets/MeshAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace MeshAPI {
    void update(MeshHandle handle, const MeshData &data) {
        if (!Internal::requireMainThreadApi("MeshAPI::update")) { return; }
        Panda::ExternalCalls::mesh_Update(
            handle.id, data.vertices.data(), data.vertices.size(), data.indices.data(), data.indices.size()
        );
    }
} // namespace MeshAPI

} // namespace Bamboo