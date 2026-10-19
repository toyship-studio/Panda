#pragma once

#include <Foundation/GraphicsAPI.hpp>
#include <Miren/Miren.hpp>
#include "Panda/Base/UUID.hpp"
#include <array>
#include <cstdint>
#include <string>

#include "Panda/Assets/Base/AssetInfo.hpp"
#include "Panda/Assets/Base/Path.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Renderer/ImportedMeshData.hpp"

namespace Panda {

class World;

struct FileData {
    Foundation::Memory memory;
    uint32_t size = 0;
};

class AssetImporterBase final {
public:
    static Miren::TextureCreate load2DTexture(const path_t &path); // path relative to Resources/
    static Miren::TextureCreate load2DTextureFile(const path_t &absolutePath);
    // Heightmap image (absolute path) → grayscale samples [0..1] (16-bit PNGs are read
    // without precision loss). false — the file failed to open/decode.
    static bool loadHeightmapSamples(
        const path_t &absolutePath, std::vector<float> &outSamples, uint32_t &outWidth, uint32_t &outHeight
    );
    static Miren::TextureCreate loadCookedTexture(const path_t &path);
    static bool saveCookedTexture(const path_t &path, const Miren::TextureCreate &create);
    static path_t getCookedTextureAssetPath(const path_t &sourcePath);
    // Shader include root (`#include "panda/…"`): default-shaders/include in Resources —
    // the editor and export expand project shaders with it (MirenTools::expandHlslIncludes).
    static path_t getShaderIncludeRoot();
    // Expanded (include-free) source — the compilers' input; mtime = the last real edit of
    // the source or of an include.
    static path_t getIntermediateShaderExpandedPath(const path_t &sourcePath);
    static path_t getIntermediateShaderSpvPath(const path_t &sourcePath);
    static path_t getCookedShaderAssetPath(const path_t &sourcePath, Foundation::GraphicsAPI backend);
    static path_t getShaderReflectionAssetPath(const path_t &fragmentSourcePath);
    static FileData loadTextData(const path_t &path);
    static FileData loadBinaryData(const path_t &path);
    static Miren::ProgramCreate loadShaderProgram(const path_t &vertexPath, const path_t &fragmentPath);
    static Foundation::Memory loadData(const path_t &path);
    // Returns the id of the created root entity (0 — gltf read error).
    static UUID addGltfSceneToWorld(const path_t &path, AssetRef<ImportedMeshAsset> asset, World &world);
    static std::vector<ImportedMeshData> loadGltfMesh(const path_t &path);
    static AssetInfo::MeshSettings loadGltfMeshMeta(const path_t &gltfPath, const path_t &projectPath);
    static path_t
    getGltfMaterialAssetPath(const path_t &gltfPath, const std::string &materialName, size_t materialIndex);
};

} // namespace Panda
