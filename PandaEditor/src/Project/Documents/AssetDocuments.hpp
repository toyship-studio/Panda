#pragma once

#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Panda/Renderer/MaterialData.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <unordered_map>
#include <vector>

namespace Panda {

class AssetHandlerEditor;

// The asset document layer: what is "unsaved", where Save writes, how it is reverted and what goes
// into undo. Lives ON TOP of the asset database (AssetHandlerEditor — registry/scan/loading):
// the database matches ids with files and objects, the documents do the editing. Access is through
// handler->documents().
class AssetDocuments final {
public:
    explicit AssetDocuments(AssetHandlerEditor &handler)
        : m_handler(handler)
        , m_jsonEncoder(true) {}

    // --- Dirty state and saving (editor tabs, Cmd+S, close gates) ---
    bool isAssetChanged(AssetId id);
    bool saveAsset(AssetId id);
    bool hasUnsavedAssets();
    int saveChangedAssets();
    std::vector<AssetId> collectChangedAssets(); // for the adaptive texts of the save dialogs

    // --- Undo stack of a document (one per document; the world has its own in World). Lives for
    // the project session; reload and operations that recreate the data reset it — the history
    // describes the previous data. ---
    CommandHistory &undoStackFor(AssetId id) {
        return m_undoStacks[id];
    }
    CommandHistory *undoStackIfExists(AssetId id) {
        auto it = m_undoStacks.find(id);
        return it != m_undoStacks.end() ? &it->second : nullptr;
    }
    void clearUndoStack(AssetId id) {
        m_undoStacks.erase(id);
    }
    void clearAllUndoStacks() {
        m_undoStacks.clear();
    }

    // --- Document edits ---
    // Changing the material shader: built-in shaders have no SPIRV reflection — the fields
    // are synchronized with the default material template. The file is not written (dirty until Save).
    // false — the shader is already set.
    bool setMaterialShader(AssetId materialId, AssetId shaderId);

    // --- Writing document files ---
    bool saveMaterial(const AssetInfo &info, const MaterialData &materialData);
    bool savePostprocessing(const AssetInfo &info, PostprocessingAsset &asset);

    // --- Factories of new documents (unique name in the directory; return the id, 0 — error) ---
    AssetId createMaterialAsset(const path_t &directory, AssetId shaderId);
    AssetId createPostprocessingAsset(const path_t &directory);
    // A file created by the editor part of a package (EditorExtensions::assetCreators): registration.
    AssetId registerCreatedAsset(const path_t &path);
    bool createPostprocessShader(const path_t &directory); // a NAME.vert/frag.hlsl pair from templates

private:
    AssetHandlerEditor &m_handler;
    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    std::unordered_map<AssetId, CommandHistory> m_undoStacks;
};

} // namespace Panda
