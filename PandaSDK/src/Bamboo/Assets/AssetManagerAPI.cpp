#include "Bamboo/Assets/AssetManagerAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace AssetManagerAPI {
    MeshHandle createMesh() {
        if (!Internal::requireMainThreadApi("AssetManagerAPI::createMesh")) { return {}; }
        Panda::Handle meshHandle;
        Panda::ExternalCalls::mesh_Create(&meshHandle);
        return MeshHandle(meshHandle);
    }

    void deleteMesh(MeshHandle mesh) {
        if (!Internal::requireMainThreadApi("AssetManagerAPI::deleteMesh")) { return; }
        Panda::ExternalCalls::mesh_Delete(mesh.id);
    }

    TextureHandle createTextureRGBA8(uint32_t width, uint32_t height) {
        if (!Internal::requireMainThreadApi("AssetManagerAPI::createTextureRGBA8")) { return {}; }
        Panda::Handle textureHandle;
        Panda::ExternalCalls::texture_CreateRGBA8(width, height, &textureHandle);
        return TextureHandle{textureHandle}; // an aggregate: braces build on every compiler
    }

    void deleteTexture(TextureHandle texture) {
        if (!Internal::requireMainThreadApi("AssetManagerAPI::deleteTexture")) { return; }
        Panda::ExternalCalls::texture_Delete(texture.id);
    }

} // namespace AssetManagerAPI

} // namespace Bamboo