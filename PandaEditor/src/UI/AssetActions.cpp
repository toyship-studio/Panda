#include "UI/AssetActions.hpp"

#include "App/EditorLayer.hpp"
#include "Preview/AssetPreviews.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorController.hpp"
#include "UI/EditorDialogs.hpp"
#include "UI/EditorModelHelper.hpp"
#include "UI/EntityActions.hpp"
#include "UI/WorldTabs.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Selection.hpp>
#include <Fern/Files/AtomicWrite.hpp>
#include <PandaUI/Core/Clipboard.hpp>

#include <Panda/Prefabs/Prefabs.hpp>

#include <algorithm>
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
    if (assets.size() == 1 && handler && !handler->documentFilePath(assets.front()).empty()) {
        m_workspace.worldTabs().open(assets.front(), /*preview*/ true);
    }
}

void AssetActions::contentDidRequestOpenWorld(Panda::AssetId world) {
    m_workspace.worldTabs().open(world, /*preview*/ false);
}

void AssetActions::contentDidRequestAddToWorld(const std::vector<Panda::AssetId> &assets) {
    // Models and prefabs become entities of the world: each kind its own way.
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    std::vector<Panda::AssetId> meshes, prefabs;
    for (Panda::AssetId asset : assets) {
        (handler && handler->getInfo(asset).type == Panda::AssetType::PREFAB ? prefabs : meshes).push_back(asset);
    }
    if (!meshes.empty()) { m_workspace.entities().instantiateMeshes(meshes); }
    if (!prefabs.empty()) { m_workspace.entities().instantiatePrefabs(prefabs); }
}

void AssetActions::contentDidRequestOpenExternally(Panda::AssetId asset) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    const Panda::path_t relative = handler->assetPath(asset);
    if (!relative.empty()) { m_workspace.openInExternalEditor(handler->getProjectPath() / relative); }
}

namespace {

    // A copy of a file, or of a folder with what is in it — but the sidecars (`.pmeta`, the ids of
    // another project: the copies are new assets of this one) and the hidden files (.DS_Store).
    bool copyIntoProject(const std::filesystem::path &source, const std::filesystem::path &target, std::error_code &error) {
        namespace fs = std::filesystem;
        if (!fs::is_directory(source, error)) { return fs::copy_file(source, target, fs::copy_options::none, error); }
        if (!fs::create_directory(target, error)) { return false; }
        for (auto it = fs::recursive_directory_iterator(source, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
            const std::string name = it->path().filename().string();
            if (name.empty() || name.front() == '.' || name.ends_with(".pmeta")) {
                if (it->is_directory(error)) { it.disable_recursion_pending(); }
                continue;
            }
            const fs::path destination = target / fs::relative(it->path(), source, error);
            if (error) { return false; }
            if (it->is_directory(error)) {
                fs::create_directories(destination, error);
            } else {
                fs::copy_file(it->path(), destination, fs::copy_options::none, error);
            }
            if (error) { return false; }
        }
        return !error;
    }

    bool isInside(const std::filesystem::path &path, const std::filesystem::path &folder) {
        const auto mismatch = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end());
        return mismatch.first == folder.end();
    }

} // namespace

// Files of the file manager become files of the project: copied into the folder under the name
// they had, or a free one ("bark 2.png"; a shader keeps the pair of its name: "water 2.vert.hlsl"),
// then registered by the scan like any file put into Assets, selected and shown — the user sees
// where they went. A file of the project itself is not copied again: a copy is another asset.
void AssetActions::contentDidRequestImportFiles(const std::vector<Panda::path_t> &files, const Panda::path_t &directory) {
    namespace fs = std::filesystem;
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler || directory.empty()) { return; }
    std::error_code error;
    const fs::path assets = fs::weakly_canonical(handler->assetsPath(), error);
    std::vector<fs::path> added;
    for (const fs::path &source : files) {
        const fs::path canonical = fs::weakly_canonical(source, error);
        const std::string name = source.filename().string();
        if (error || name.empty() || !fs::exists(canonical, error)) { continue; }
        if (isInside(canonical, assets)) {
            LOG_WARN_EDITOR("%s is a file of the project already: it was not copied.", name.c_str());
            continue;
        }
        const bool folder = fs::is_directory(canonical, error);
        const auto [base, extension] = folder ? std::pair<std::string, std::string>(name, std::string())
                                              : Panda::AssetHandlerEditor::splitName(name);
        const fs::path target = Editor::uniqueAssetPath(directory, base, extension);
        if (!copyIntoProject(canonical, target, error)) {
            LOG_ERROR_EDITOR("%s was not added to the project: %s.", name.c_str(), error.message().c_str());
            fs::remove_all(target, error); // half a folder is not left behind
            continue;
        }
        added.push_back(target);
    }
    if (added.empty()) { return; }
    handler->rescanProject();
    m_workspace.refreshAssetPanels();
    std::vector<Panda::AssetId> ids;
    for (const fs::path &path : added) {
        if (const Panda::AssetId id = handler->getAssetId(path)) { ids.push_back(id); }
    }
    if (!ids.empty()) {
        m_workspace.revealAsset(ids.front()); // the folder first: going into it clears the highlight of the grid
        m_workspace.selectAssets(ids);
    }
    const std::string what = added.size() == 1 ? added.front().filename().string() : std::to_string(added.size()) + " files";
    const fs::path where = fs::relative(directory, handler->getProjectPath(), error);
    LOG_INFO_EDITOR("Added %s to %s.", what.c_str(), where.generic_string().c_str());
}

void AssetActions::contentDidRequestDuplicate(const std::vector<Panda::AssetId> &assets) {
    namespace fs = std::filesystem;
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return; }
    const fs::path project = handler->getProjectPath();
    std::vector<Panda::AssetId> copies;
    for (Panda::AssetId id : assets) {
        if (handler->assetPath(id).empty()) { continue; }
        const fs::path main = project / handler->assetPath(id);
        const std::string name = main.filename().string();
        // The name of the main file is the name of every file of the asset (a shader's pair).
        const auto [base, extension] = Panda::AssetHandlerEditor::splitName(name);
        const fs::path copyMain = Editor::uniqueAssetPath(main.parent_path(), base, extension);
        const std::string copyBase = Panda::AssetHandlerEditor::splitName(copyMain.filename().string()).first;
        std::error_code error;
        std::vector<fs::path> written;
        for (const fs::path &file : handler->getAssetPaths(id)) {
            const std::string fileName = file.filename().string();
            if (fileName.rfind(base, 0) != 0) { continue; }
            const fs::path target = file.parent_path() / (copyBase + fileName.substr(base.size()));
            if (!fs::copy_file(file, target, fs::copy_options::none, error)) { break; }
            written.push_back(target);
        }
        if (error) {
            LOG_ERROR_EDITOR("%s was not duplicated: %s.", name.c_str(), error.message().c_str());
            for (const fs::path &file : written) { fs::remove(file, error); }
            continue;
        }
        // A new id, and the settings of the original: the shader of a material, the import of a
        // texture — what the sidecar holds besides the id.
        handler->registerAsset(copyMain);
        if (const Panda::AssetId copy = handler->getAssetId(copyMain)) {
            Panda::AssetInfo info = handler->getInfo(id);
            info.id = copy;
            handler->updateInfo(copy, info);
            copies.push_back(copy);
        }
    }
    if (copies.empty()) { return; }
    m_workspace.refreshAssetPanels();
    m_workspace.revealAsset(copies.front()); // the folder first: going into it clears the highlight of the grid
    m_workspace.selectAssets(copies);
}

void AssetActions::contentDidShowFolder(const Panda::path_t &folder) {
    const Panda::path_t project = m_app.project().getOpenedProjectPath();
    if (project.empty()) { return; }
    std::error_code error;
    const Panda::path_t relative = std::filesystem::relative(folder, project, error);
    if (!error) { m_app.project().setContentBrowserFolder(relative.generic_string()); }
}

void AssetActions::contentDidRequestImportDialog(const Panda::path_t &directory) {
    const std::vector<Panda::path_t> files = Panda::SystemTools::openFilesDialog();
    if (!files.empty()) { contentDidRequestImportFiles(files, directory); }
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
    // A prefab starts as one entity named after its file, and opens in its tab.
    actions.push_back({"Prefab", [this, directory] {
                           Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
                           if (!handler) { return; }
                           const Panda::path_t path = uniqueAssetPath(directory, "Prefab", ".prefab");
                           Panda::World scratch;
                           Panda::Entity root = scratch.instantiateEntity();
                           root.setName(path.stem().string());
                           std::string error;
                           if (!Fern::writeFileAtomically(path, Panda::Prefabs::save(scratch, root), &error)) {
                               LOG_ERROR_EDITOR("The prefab is not made: %s.", error.c_str());
                               return;
                           }
                           const Panda::AssetId prefab = handler->documents().registerCreatedAsset(path);
                           created(prefab, "prefab");
                           if (prefab) { m_workspace.worldTabs().open(prefab, /*preview*/ false); }
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

bool AssetActions::movePath(const Panda::path_t &from, const Panda::path_t &to) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return false; }
    std::string error;
    if (!handler->movePath(from, to, &error)) {
        LOG_ERROR_EDITOR("%s was not moved: %s.", from.filename().string().c_str(), error.c_str());
        return false;
    }
    m_app.documents().fileMoved(from, to);
    return true;
}

void AssetActions::contentDidRequestRename(const Panda::path_t &path, const std::string &newName) {
    if (newName.empty() || newName.find_first_of("/\\:") != std::string::npos || newName.front() == '.') {
        LOG_ERROR_EDITOR("A name has no slashes or colons and does not begin with a dot");
        return;
    }
    std::error_code error;
    const bool folder = std::filesystem::is_directory(path, error);
    const std::string extension = folder ? std::string() : Panda::AssetHandlerEditor::splitName(path.filename().string()).second;
    const Panda::path_t target = path.parent_path() / (newName + extension);
    if (target == path || !movePath(path, target)) { return; }
    m_workspace.refreshAssetPanels();
    m_workspace.selectionChanged(); // the name on its tab may have changed
}

void AssetActions::contentDidRequestMove(const std::vector<Panda::path_t> &paths, const Panda::path_t &directory) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler || directory.empty()) { return; }
    std::vector<Panda::AssetId> moved;
    for (const Panda::path_t &path : paths) {
        if (path.parent_path() == directory) { continue; } // where it is already
        const Panda::path_t target = directory / path.filename();
        if (!movePath(path, target)) { continue; }
        if (const Panda::AssetId id = handler->getAssetId(target)) { moved.push_back(id); }
    }
    m_workspace.refreshAssetPanels();
    // The ids are the same: the selection stays on the moved assets, the browser stays where it is.
    if (!moved.empty()) { m_workspace.selectAssets(moved); }
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
        // A prefab: its open document the same way, and — below — the prefab its instances are made of.
        if (handler->getInfo(id).type == Panda::AssetType::PREFAB) { m_app.documentActions().reloadWorldAsset(id); }
        handler->reload(id);
        // The pictures of the asset are of what it was: its thumbnail and its open previews.
        if (auto *previews = Panda::AssetPreviews::shared()) { previews->invalidate(id); }
    }
    m_workspace.refreshAssetPanels();
    LOG_INFO_EDITOR("Reimported %zu %s", assets.size(), assets.size() == 1 ? "asset" : "assets");
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
