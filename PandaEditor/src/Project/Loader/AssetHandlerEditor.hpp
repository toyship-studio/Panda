#pragma once

#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include "Project/Documents/AssetDocuments.hpp"
#include "Project/Documents/FileDocument.hpp"
#include "Project/Import/AssetImportPipeline.hpp"

#include <Panda/Assets/Base/AssetHandler.hpp>
#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Prefabs/Prefabs.hpp>
#include <Panda/Renderer/MaterialData.hpp>
#include <cstdint>
#include <functional>
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
    // The handler of the editor (the engine keeps it by its own interface, GameContext); nullptr —
    // before the editor has made it.
    static AssetHandlerEditor *current();
    void registerAsset(const path_t &path);
    bool canImport(const path_t &path);
    void reload(AssetId id);
    // A prefab changed — its file was written by the editor or changed on disk, an Apply put a
    // difference into it — or is gone: it is read again, and so is every prefab that holds it,
    // and their instances in the open worlds (setInstanceWorlds) follow, each keeping what it
    // differs in (Prefabs::prefabChanged). `forced` — the Apply Forced that changed it.
    void prefabChanged(AssetId id, const Prefabs::Applied *forced = nullptr);
    // The worlds the editor holds open: where the instances of a changed prefab are.
    void setInstanceWorlds(std::function<std::vector<World *>()> worlds) {
        m_instanceWorlds = std::move(worlds);
    }
    // The project shaders compile for the shader features of the project: a changed set recompiles
    // the loaded ones in place (the same reload as after an edit of the source).
    void reloadLoadedShaders();
    AssetInfo getInfo(AssetId id);
    void updateInfo(AssetId assetId, const AssetInfo &assetInfo);
    void openProject(const path_t &path);
    void closeProject();
    // The assets of a project are the files of its Assets folder: the registry is built from that
    // tree and the Content Browser shows it. What lies elsewhere in the project — the scripts, the
    // Export project and what it builds, the pictures of a README — is no asset and gets no sidecar.
    static constexpr const char *ASSETS_FOLDER = "Assets";
    path_t assetsPath() const {
        return m_projectPath / ASSETS_FOLDER;
    }
    // Rescans the Assets folder (Reload, and the disk watch when the tree changed): new files
    // become assets, the ids of existing ones are stable (they live in the sidecars) — loaded
    // assets and tabs are not broken.
    void rescanProject();
    // Editor state of an asset outside the registry (context/architecture.md):
    // the file as the editor last read or wrote it (load/save) and whether it is open in a tab. The in-memory dirty state lives in the document model
    // (documents()) and is aggregated into the editorMeta() snapshot — a single answer point
    // per asset.
    struct AssetEditorMeta {
        DiskStamp disk;
        bool openedInTab = false;
        bool changedInMemory = false;
    };
    AssetEditorMeta editorMeta(AssetId id);
    void noteAssetDiskSync(AssetId id); // the asset file has just been read/written by the editor
    // The file of a loaded asset is not the one the asset in memory was read from. A shader is
    // more than its asset file (the fragment stage and the includes are files of their own), so
    // it is also compared by the text its sources expand to.
    bool isAssetChangedOnDisk(AssetId id);
    // What a change on disk of a path under the Assets folder (a real path) is to the registry —
    // the disk watch asks (App/DiskWatch.hpp): nothing — the editor wrote the sidecar itself, or
    // the file came and went; the content of a file — of a known asset, or of one the registry
    // does not keep (an include of a shader): the documents see it by its time; news of the tree
    // — an asset is gone, or a folder, a sidecar or a file the project imports has come:
    // rescanProject reads it.
    enum class DiskChange { None, Content, Tree };
    DiskChange classifyDiskChange(const path_t &path);
    // The loaded assets of the project (the static resources of the engine have no file of the
    // project). Their file documents are made by FileDocuments; an asset nobody loaded is read
    // fresh on its next use and has nothing in memory to save or reload.
    std::vector<AssetId> loadedProjectAssets() const;
    // An asset tab opened or closed. While its tab is open the asset stays loaded, used by the
    // world or not: what the tab edits lives in memory until Save.
    void setAssetOpenedInTab(AssetId id, bool opened);
    const path_t &getProjectPath();
    const std::map<AssetId, AssetInfo> &getRegistry();
    // The asset source is a property of the REGISTRATION (filled in by the scan from the sidecar
    // location): file + sub-path for future containers (glb). Empty — the asset is not registered.
    AssetFileReference assetSource(AssetId id) const;
    // Shortcut: the path of the source file (needed by most consumers).
    path_t assetPath(AssetId id) const {
        return assetSource(id).path;
    }
    // The file of a world asset, with the folder of the project. Empty — there is no such asset
    // or it is not a world: the log says which.
    path_t worldFilePath(AssetId id);
    // The file of an asset that opens in a tab of the center — a world or a prefab — with the
    // folder of the project. Empty — the asset is neither.
    path_t documentFilePath(AssetId id);
    std::string getAssetName(AssetId id); // the file name of the asset
    // Total size of the asset's files in the project (bytes; 0 — no files). Shown in the inspector;
    // in the future the size in the runtime Asset Package (cooked form) will stand next to it.
    uint64_t assetSizeOnDisk(AssetId id);
    // false — the asset's files are incomplete/broken (a shader without its frag pair, an uncookable
    // texture, a file deleted externally in the middle of a session — until the next scan). Export gate +
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
    bool readMeshData(AssetId id, std::vector<ImportedMeshData> &meshes) override;
    int getLoadedAssetsCount();

private:
    friend class AssetDocuments;      // layers of one pair: the registry/loaded assets/sidecars are
    friend class AssetImportPipeline; // accessible through friend, the public API does not bloat

    void registerPath(AssetId id, const path_t &relativePath); // both maps: id→path and path→id
    // Shader pair: frag is derived from vert by the *vert.hlsl -> *frag.hlsl convention.
    bool ensureShaderSourceFilesExist(AssetId id, const path_t &vertexRelPath, bool logErrors = true);
    // --- .pmeta sidecars (context/asset-files.md) ---
    void scanProject(); // builds the registry by walking the Assets folder: sidecars + newcomers + orphans
    // Adopts an asset by its sidecar: the meta paths are normalized to the actual location of the file
    // (the pair may have moved), an id conflict (a copy of the pair) gets a fresh id.
    void adoptPmeta(const path_t &dataPath);
    void savePmeta(AssetId id); // writes the asset's sidecar (individually, instead of a shared registry)
    AssetId freshAssetId() const; // a random id that is free in the registry (0 is excluded)
    void markMissingFiles(AssetId id);
    void removeMissingFiles(AssetId id);

    Asset *loadInternal(AssetId id, bool forcedReload = false) override;

    path_t m_projectPath;
    std::function<std::vector<World *>()> m_instanceWorlds;
    AssetDocuments m_documents;     // document layer (composition; reaches the database through friend)
    AssetImportPipeline m_import;   // import orchestration (composition, like documents)
    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    std::map<AssetId, AssetInfo> m_registry;
    std::unordered_map<AssetId, AssetEditorMeta> m_editorMeta;
    // The sidecars the editor wrote, by their real paths, with the time each got.
    std::unordered_map<path_t, std::filesystem::file_time_type> m_ownSidecars;
    // The assets open in tabs, held: see setAssetOpenedInTab.
    std::unordered_map<AssetId, AssetRef<Asset>> m_tabReferences;
    std::unordered_map<AssetId, AssetFileReference> m_assetSources; // id → source (the path is relative)
    // Lookup of an assetId by path. Needed for the getAssetId function
    std::unordered_map<path_t, AssetId> m_registeredAssets;
    std::unordered_set<AssetId> m_missingFiles; // assets with incomplete/broken files
};

} // namespace Panda
