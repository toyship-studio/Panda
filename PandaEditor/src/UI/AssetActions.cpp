#include "UI/AssetActions.hpp"

#include "App/EditorLayer.hpp"
#include "Preview/AssetPreviews.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "UI/EditorController.hpp"
#include "UI/EditorDialogs.hpp"
#include "UI/EditorModelHelper.hpp"
#include "UI/EntityActions.hpp"
#include "UI/WorldTabs.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Selection.hpp>
#include <PandaUI/Core/Clipboard.hpp>

#include <filesystem>

namespace Editor {

using namespace PandaUI;

AssetActions::AssetActions(EditorController &workspace, Panda::EditorLayer &app)
    : m_workspace(workspace)
    , m_app(app) {}

void AssetActions::contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) {
    m_workspace.selectAssets(assets);
    // A world is a document, not an asset with parameters: a click shows it the way a click on a
    // file shows it in a code editor — in the preview tab.
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (assets.size() == 1 && handler && handler->getInfo(assets.front()).type == Panda::AssetType::WORLD) {
        m_workspace.worldTabs().open(assets.front(), /*preview*/ true);
    }
}

void AssetActions::contentDidRequestOpenWorld(Panda::AssetId world) {
    m_workspace.worldTabs().open(world, /*preview*/ false);
}

void AssetActions::contentDidRequestAddToWorld(const std::vector<Panda::AssetId> &assets) {
    m_workspace.entities().instantiateMeshes(assets);
}

void AssetActions::contentAddFileItems(Menu &menu, Panda::AssetId asset) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    const Panda::path_t relative = handler->assetPath(asset);
    if (relative.empty()) { return; }
    m_workspace.addFileItems(menu, handler->getProjectPath() / relative);
    // The id a world or a material references the asset by (hand edits of the text files).
    const std::string id = std::to_string(static_cast<uint32_t>(asset));
    menu.addItem("Copy ID: " + id, [id] { setClipboardText(id); });
    menu.addSeparator();
}

/* ------------------------------ Creating ------------------------------ */

CreateActions AssetActions::contentCreateActions(const Panda::path_t &directory) {
    CreateActions actions;
    // A material starts with the first built-in shader; its tab changes it.
    actions.push_back({"Material", [this, directory] {
                           Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
                           if (!handler) { return; }
                           const Panda::AssetId shader = builtinShaderOptions().front().first;
                           created(handler->documents().createMaterialAsset(directory, shader), "material");
                       }});
    actions.push_back({"Postprocessing", [this, directory] {
                           Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
                           if (!handler) { return; }
                           created(handler->documents().createPostprocessingAsset(directory), "postprocessing");
                       }});
    actions.push_back({"Postprocess Shader", [this, directory] {
                           Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
                           if (handler && handler->documents().createPostprocessShader(directory)) {
                               m_workspace.refreshAssetPanels();
                           }
                       }});
    // The assets of the modules; the registry does not change while the project is open.
    for (const AssetCreator &creator : editorExtensions().assetCreators) {
        const AssetCreator *module = &creator;
        actions.push_back({creator.title, [this, directory, module] {
                               Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
                               if (!handler || module->create == nullptr) { return; }
                               // The contribution writes the file; the registration and the
                               // selection are the same for every asset.
                               const Panda::path_t path = module->create(directory);
                               created(
                                   path.empty() ? Panda::AssetId(0) : handler->documents().registerCreatedAsset(path),
                                   module->title
                               );
                           }});
    }
    return actions;
}

void AssetActions::created(Panda::AssetId asset, const std::string &what) {
    if (!asset) {
        LOG_ERROR_EDITOR("Failed to create %s", what.c_str());
        return;
    }
    m_workspace.refreshAssetPanels();
    m_workspace.selectAssets({asset}); // the tab of the new asset opens right away
}

void AssetActions::contentDidRequestNewFolder(const Panda::path_t &directory, const std::string &name) {
    if (directory.empty() || name.empty() || name.find_first_of("/\\:") != std::string::npos || name.front() == '.') {
        LOG_ERROR_EDITOR("A folder name has no slashes or colons and does not begin with a dot");
        return;
    }
    const Panda::path_t folder = directory / name;
    std::error_code error;
    if (std::filesystem::exists(folder, error)) {
        LOG_ERROR_EDITOR("The folder %s already exists", name.c_str());
        return;
    }
    if (!std::filesystem::create_directory(folder, error)) {
        LOG_ERROR_EDITOR("The folder %s could not be made: %s", name.c_str(), error.message().c_str());
        return;
    }
    m_workspace.refreshAssetPanels();
}

/* ------------------------------ Changing ------------------------------ */

void AssetActions::contentDidRequestRename(Panda::AssetId asset, const std::string &newName) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    if (!handler->renameAsset(asset, newName)) {
        LOG_ERROR_EDITOR("Failed to rename asset");
        return;
    }
    m_workspace.refreshAssetPanels();
    m_workspace.selectionChanged(); // the name on its tab may have changed
}

void AssetActions::contentDidRequestImport(const std::vector<Panda::AssetId> &assets) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    for (Panda::AssetId id : assets) {
        // Reimport of a world is a reread from disk when it is open: a regular reload is a no-op
        // for a world (an open world is a file document — FileDocuments — not an asset of the
        // loading registry).
        if (handler->getInfo(id).type == Panda::AssetType::WORLD) {
            m_app.documentActions().reloadWorldAsset(id);
            continue;
        }
        handler->reload(id);
        // The pictures of the asset are of what it was: its thumbnail and its open previews.
        if (auto *previews = Panda::AssetPreviews::shared()) { previews->invalidate(id); }
    }
    m_workspace.refreshAssetPanels();
    LOG_INFO_EDITOR("Reimported %zu %s", assets.size(), assets.size() == 1 ? "asset" : "assets");
}

void AssetActions::contentDidRequestRefresh() {
    // Tree rescan: new files become assets, the ids of existing ones are stable (sidecars).
    if (Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current()) { handler->rescanProject(); }
    m_workspace.refreshAssetPanels();
}

/* ------------------------------ Deleting ------------------------------ */

// File = asset: the files go from disk (a rescan would bring the asset back with a new id), and
// with them the files derived from the asset and the loaded object. A world among them that is
// open is closed first — a tab does not outlive its file, and the game that plays that world stops.
void AssetActions::removeAssets(const std::vector<Panda::AssetId> &assets) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    for (Panda::AssetId id : assets) {
        if (Panda::WorldFile *world = m_app.documents().findWorld(handler->getProjectPath() / handler->assetPath(id))) {
            m_app.closeWorld(*world);
        }
    }
    m_workspace.worldTabs().sync();
    for (Panda::AssetId id : assets) { handler->removeAsset(id); }
    selection().selectAssets({});
    m_workspace.refreshAssetPanels();
    m_workspace.selectionChanged();
}

void AssetActions::contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler || assets.empty()) { return; }
    const std::string title = assets.size() == 1 ? "Delete \"" + handler->getAssetName(assets.front()) + "\"?"
                                                 : "Delete " + std::to_string(assets.size()) + " assets?";
    showConfirmDialog(
        m_workspace.context(), title, "Files will be removed from disk. This cannot be undone.", "Delete",
        /*destructive*/ true,
        [this, assets] {
            removeAssets(assets);
            LOG_INFO_EDITOR("Deleted %zu %s", assets.size(), assets.size() == 1 ? "asset" : "assets");
        }
    );
}

// A folder goes with everything in it: its assets the way any asset is deleted (removeAssets),
// then the directory itself with what the editor does not know (files of other formats).
void AssetActions::contentDidRequestDeleteFolder(const Panda::path_t &folder) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler || folder.empty()) { return; }
    std::vector<Panda::AssetId> inside;
    for (const auto &[id, info] : handler->getRegistry()) {
        const Panda::path_t relative = (handler->getProjectPath() / handler->assetPath(id)).lexically_relative(folder);
        if (!relative.empty() && *relative.begin() != "..") { inside.push_back(id); }
    }
    const std::string name = folder.filename().string();
    const std::string count = inside.empty()       ? "The folder"
                              : inside.size() == 1 ? "The folder and the asset in it"
                                                   : "The folder and the " + std::to_string(inside.size()) + " assets in it";
    showConfirmDialog(
        m_workspace.context(), "Delete \"" + name + "\"?", count + " will be removed from disk. This cannot be undone.",
        "Delete", /*destructive*/ true,
        [this, folder, inside, name] {
            removeAssets(inside);
            std::error_code error;
            std::filesystem::remove_all(folder, error);
            m_workspace.refreshAssetPanels(); // the directory is gone only now
            if (error) {
                LOG_ERROR_EDITOR("The folder %s could not be removed: %s", name.c_str(), error.message().c_str());
            } else {
                LOG_INFO_EDITOR("Deleted the folder %s", name.c_str());
            }
        }
    );
}

} // namespace Editor
