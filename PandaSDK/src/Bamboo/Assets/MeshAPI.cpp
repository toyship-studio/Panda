#include "Bamboo/Assets/MeshAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace MeshAPI {
    void update(MeshHandle handle, const MeshData &data) {
        if (!Internal::requireMainThreadApi("MeshAPI::update")) { return; }
        Panda::ExternalCalls::mesh_Update(
            handle.id(), data.vertices.data(), data.vertices.size(), data.indices.data(), data.indices.size()
        );
    }

    bool read(MeshHandle handle, MeshData &data) {
        data.vertices.clear();
        data.indices.clear();
        if (!Internal::requireMainThreadApi("MeshAPI::read")) { return false; }
        static_assert(sizeof(Vertex) == 56, "a vertex crosses the script ABI as the engine lays it out");
        uint32_t vertexCount = 0, indexCount = 0;
        if (!Panda::ExternalCalls::mesh_Read(handle.id(), nullptr, 0, &vertexCount, nullptr, 0, &indexCount)) { return false; }
        data.vertices.resize(vertexCount);
        data.indices.resize(indexCount);
        if (vertexCount == 0 || indexCount == 0) { return true; }
        return Panda::ExternalCalls::mesh_Read(
            handle.id(), data.vertices.data(), vertexCount, &vertexCount, data.indices.data(), indexCount, &indexCount
        );
    }
} // namespace MeshAPI

} // namespace Bamboo