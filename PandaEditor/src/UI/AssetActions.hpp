#pragma once

#include "UI/CreateAction.hpp"
#include "UI/Panels/ContentBrowserView.hpp"

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <string>
#include <vector>

namespace Panda {
class EditorLayer;
}

namespace Editor {

class EditorController;

// What the user does to the assets of the project: the intents of the Content Browser — select,
// create, rename, reimport, delete, for assets and for folders, and open a world. The files and
// the registry are the asset handler's (AssetHandlerEditor); here is what the editor adds around
// them — the question before a deletion, the selection of what was made, the panels that follow.
class AssetActions final : public ContentBrowserOutput {
public:
    AssetActions(EditorController &workspace, Panda::EditorLayer &app);

    // ContentBrowserOutput.
    void contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) override;
    // Asks, then removes: the files go from disk, and with them what was derived from them and
    // what was loaded. A world among them that is open is closed first.
    void contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestImport(const std::vector<Panda::AssetId> &assets) override;
    void contentAddFileItems(PandaUI::Menu &menu, Panda::AssetId asset) override;
    void contentDidRequestAddToWorld(const std::vector<Panda::AssetId> &assets) override;
    void contentDidRequestOpenWorld(Panda::AssetId world) override;
    void contentDidRequestOpenExternally(Panda::AssetId asset) override;
    void contentDidRequestImportFiles(const std::vector<Panda::path_t> &files, const Panda::path_t &directory) override;
    void contentDidRequestImportDialog(const Panda::path_t &directory) override;
    void contentDidShowFolder(const Panda::path_t &folder) override;
    void contentDidRequestMove(const std::vector<Panda::path_t> &paths, const Panda::path_t &directory) override;

private:
    // A file or a folder of Assets to another path, the documents following it (FileDocuments::fileMoved);
    // false — the log says why.
    bool movePath(const Panda::path_t &from, const Panda::path_t &to);

public:
    void contentDidRequestDuplicate(const std::vector<Panda::AssetId> &assets) override;
    CreateActions contentCreateActions(const Panda::path_t &directory) override;
    void contentDidRequestRename(const Panda::path_t &path, const std::string &newName) override;
    void contentDidRequestNewFolder(const Panda::path_t &directory, const std::string &name) override;
    void contentDidRequestDeleteFolder(const Panda::path_t &folder) override;

private:
    void removeAssets(const std::vector<Panda::AssetId> &assets); // what a confirmed deletion does
    // A new asset is in the registry: the browser shows it and it becomes the selection (its tab
    // opens). 0 — it was not made: `what` goes to the log.
    void created(Panda::AssetId asset, const std::string &what);

    EditorController &m_workspace;
    Panda::EditorLayer &m_app;
};

} // namespace Editor
