#pragma once

#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <cstdint>
#include <map>

namespace Panda {

class AssetHandlerEditor;

// Import orchestration: how a foreign file becomes a project asset. Registration of new files
// by extension (meta + sidecar), texture cooking, the GLTF pipeline (textures by sRGB role,
// .mat generation, meta rebuild on reimport), texture import settings. Lives ON TOP of
// the asset database (AssetHandlerEditor — registry/scan/loading); access is through
// handler->import(). The static conversion utilities stay in AssetImporterBase.
class AssetImportPipeline final {
public:
    using RegisterFunc = void (AssetImportPipeline::*)(const path_t &path);

    explicit AssetImportPipeline(AssetHandlerEditor &handler);

    // The extension is known: a built-in import or a file-based kind from the registry (plugins).
    bool supportsExtension(const path_t &extension) const;
    // Full import of a new file (dispatch by extension): meta, sidecar, for gltf —
    // textures and materials. The caller guarantees that the file is not registered yet.
    void registerAsset(const path_t &path);

    // Cooks the texture if necessary (missing/outdated). The cook is a property of the source, not
    // of the meta: the cooked path is derived from the location (getCookedTextureAssetPath).
    bool ensureTextureIsCooked(const path_t &sourceRelPath, bool forceRecook = false);
    // Rebuilds the mesh meta from the gltf (reimport); slot bindings survive BY NAME.
    bool refreshMeshSettings(AssetId assetId);
    // Imports the textures of glTF materials with sRGB/linear roles per the spec.
    void importTexturesFromGltfMeta(const AssetInfo::MeshSettings &meta);
    // Generates/updates the .mat files for the materials of the mesh meta; EMPTY slots are bound
    // to the generated materials (user replacements are not overwritten).
    size_t createMaterialsFromGltf(AssetId meshAssetId);

    // Texture import settings: applied immediately (no document dirty state), persisted
    // into the sidecar and reload the asset.
    void setTextureSrgb(AssetId id, bool srgb);
    void setTextureFiltering(AssetId id, Miren::TextureFiltering minFiltering, Miren::TextureFiltering magFiltering);
    void setTextureMips(AssetId id, uint32_t numMips);

private:
    // A wrapper for the extension registry: color textures are sRGB by default.
    // A separate name, not an overload — the registry stores a member-fn-pointer.
    void importTextureSourceAsset(const path_t &path);
    // Radiance .hdr (environment map): linear data, the cook is RGBA16F.
    void importHdrTextureSourceAsset(const path_t &path);
    void importTextureSourceAssetAs(const path_t &path, bool srgb);
    void registerShaderAsset(const path_t &path);
    void registerMaterialAsset(const path_t &path);
    void registerMeshAsset(const path_t &path);
    void registerWorldAsset(const path_t &path);
    void registerPostprocessingAsset(const path_t &path);
    // A file asset without settings: id and location (world, postfx, plugin kinds).
    void registerPlainAsset(const path_t &path, AssetType type);

    AssetHandlerEditor &m_handler;
    std::map<path_t, RegisterFunc> m_registerByExtension;
};

} // namespace Panda
