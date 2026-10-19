#pragma once

#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include "Project/Documents/AssetDocuments.hpp"
#include "Project/Import/AssetImportPipeline.hpp"

#include <Panda/Assets/Base/AssetHandler.hpp>
#include <Panda/Renderer/MaterialData.hpp>
#include <map>

namespace Panda {

class AssetHandlerEditor;

// The project asset registry. The truth is decentralized across files: EVERY asset has a sidecar
// X.<ext>.pmeta (JSON AssetInfo: id + meta), the data is in the asset file. The registry lives only
// in memory and is built by scanning the project tree on open (a file without a sidecar is a new
// asset, a sidecar without a file is garbage). The id travels together with the file: moves in
// Finder do not break references; a copy of the pair gets a fresh id on scan.
class AssetHandlerEditor final : public AssetHandler {
public:
    AssetHandlerEditor();
    void registerAsset(const path_t &path);
    bool canImport(const path_t &path);
    void reload(AssetId id);
    AssetInfo getInfo(AssetId id);
    void updateInfo(AssetId assetId, const AssetInfo &assetInfo);
    void openProject(const path_t &path);
    void closeProject();
    // Rescans the project tree (the Refresh button): new files become assets, the ids of existing
    // ones are stable (they live in the sidecars) — loaded assets and tabs are not broken.
    void rescanProject();
    // Editor state of an asset outside the registry (context/architecture.md):
    // the file mtime at the moment of the last synchronization with the disk (load/save) and
    // whether it is open in a tab. The in-memory dirty state lives in the document model
    // (documents()) and is aggregated into the editorMeta() snapshot — a single answer point
    // per asset.
    struct AssetEditorMeta {
        std::filesystem::file_time_type diskTime{};
        bool openedInTab = false;
        bool changedInMemory = false;
    };
    AssetEditorMeta editorMeta(AssetId id);
    void noteAssetDiskSync(AssetId id); // the asset file has just been read/written by the editor
    void setAssetOpenedInTab(AssetId id, bool opened);
    // Loaded assets whose file on disk is newer than the last synchronization (Reload).
    std::vector<AssetId> collectAssetsChangedOnDisk();
    const path_t &getProjectPath();
    const std::map<AssetId, AssetInfo> &getRegistry();
    // The asset source is a property of the REGISTRATION (filled in by the scan from the sidecar
    // location): file + sub-path for future containers (glb). Empty — the asset is not registered.
    AssetFileReference assetSource(AssetId id) const;
    // Shortcut: the path of the source file (needed by most consumers).
    path_t assetPath(AssetId id) const {
        return assetSource(id).path;
    }
    std::string getAssetName(AssetId id); // the file name of the asset
    // Total size of the asset's files in the project (bytes; 0 — no files). Shown in the inspector;
    // in the future the size in the runtime Asset Package (cooked form) will stand next to it.
    uint64_t assetSizeOnDisk(AssetId id);
    // false — the asset's files are incomplete/broken (a shader without its frag pair, an uncookable
    // texture, a file deleted externally in the middle of a session — until Refresh). Export gate +
    // the Missing column.
    bool assetFilesExist(AssetId id);
    void removeAsset(AssetId id);
    // The document layer (dirty state/Save/undo/factories) is a separate role ON TOP of the database.
    AssetDocuments &documents() {
        return m_documents;
    }
    // Import orchestration (registration of new files, cooking, the gltf pipeline, import settings).
    AssetImportPipeline &import() {
        return m_import;
    }
    // Renames the asset file (material/postprocessing — single-file ones). newFileName has no extension.
    bool renameAsset(AssetId id, const std::string &newFileName);
    // Field template of a built-in shader (inputs of the default material) or null for project shaders.
    static MaterialData *builtinShaderFieldTemplate(AssetId shaderId);
    // Synchronizes the fields with the template: removes extra ones, adds missing ones, keeps the
    // values of matching ones. Public: the exporter needs them too (the .mat on disk may lag behind
    // the template).
    static void syncMaterialFieldsWithTemplate(MaterialData &data, MaterialData tmpl);

    UUID getAssetId(path_t path) override;
    std::unordered_set<path_t> getAssetPaths(AssetId id) override;
    bool hasAsset(AssetId id) const override;
    bool readTexturePixelsRGBA8(
        AssetId id, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
    ) override;
    int getLoadedAssetsCount();

private:
    friend class AssetDocuments;      // layers of one pair: the registry/loaded assets/sidecars are
    friend class AssetImportPipeline; // accessible through friend, the public API does not bloat

    AssetInfo &getInfoRef(AssetId id);
    void registerPath(AssetId id, const path_t &relativePath); // both maps: id→path and path→id
    // Shader pair: frag is derived from vert by the *vert.hlsl -> *frag.hlsl convention.
    bool ensureShaderSourceFilesExist(AssetId id, const path_t &vertexRelPath, bool logErrors = true);
    // --- .pmeta sidecars (context/asset-files.md) ---
    void scanProject(); // builds the registry by walking the tree: sidecars + newcomers + orphans
    // Adopts an asset by its sidecar: the meta paths are normalized to the actual location of the file
    // (the pair may have moved), an id conflict (a copy of the pair) gets a fresh id.
    void adoptPmeta(const path_t &dataPath);
    void savePmeta(AssetId id); // writes the asset's sidecar (individually, instead of a shared registry)
    AssetId freshAssetId() const; // a random id that is free in the registry (0 is excluded)
    void markMissingFiles(AssetId id);
    void removeMissingFiles(AssetId id);

    Asset *loadInternal(AssetId id, bool forcedReload = false) override;

    path_t m_projectPath;
    AssetDocuments m_documents;     // document layer (composition; reaches the database through friend)
    AssetImportPipeline m_import;   // import orchestration (composition, like documents)
    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    std::map<AssetId, AssetInfo> m_registry;
    std::unordered_map<AssetId, AssetEditorMeta> m_editorMeta;
    std::unordered_map<AssetId, AssetFileReference> m_assetSources; // id → source (the path is relative)
    // Lookup of an assetId by path. Needed for the getAssetId function
    std::unordered_map<path_t, AssetId> m_registeredAssets;
    std::unordered_set<AssetId> m_missingFiles; // assets with incomplete/broken files
};

} // namespace Panda
