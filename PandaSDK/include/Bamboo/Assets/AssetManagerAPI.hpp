#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

namespace AssetManagerAPI {
    MeshHandle createMesh();
    void deleteMesh(MeshHandle mesh);
    // Runtime-updatable texture (lightmaps, minimaps); update via TextureAPI::updateRGBA8.
    TextureHandle createTextureRGBA8(uint32_t width, uint32_t height);
    void deleteTexture(TextureHandle texture);
} // namespace AssetManagerAPI

} // namespace Bamboo