#pragma once

#include "UI/CreateAction.hpp"
#include "UI/Panels/ContentBrowserDataSource.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetCreator
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// Intents of the content browser (implemented by Editor::AssetActions). The identity of registered
// assets is the engine AssetId; new (not yet registered) files are addressed by paths.
class ContentBrowserOutput {
public:
    virtual ~ContentBrowserOutput() = default;
    virtual void contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) = 0; // empty = clear selection
    virtual void contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) = 0;
    virtual void contentDidRequestImport(const std::vector<Panda::AssetId> &assets) = 0; // reimport
    // The file items of an asset's context menu (the path, the external editor, the file manager).
    virtual void contentAddFileItems(PandaUI::Menu &menu, Panda::AssetId asset) = 0;
    virtual void contentDidRequestRefresh() = 0; // Refresh: registry rescan + panel rebuild
    virtual void contentDidRequestAddToWorld(const std::vector<Panda::AssetId> &assets) = 0; // a model becomes entities
    virtual void contentDidRequestOpenWorld(Panda::AssetId assetId) = 0; // open the .world as the current world
    // The assets that can be created in the directory (RMB on the grid background): the same list
    // as in File → New.
    virtual CreateActions contentCreateActions(const Panda::path_t &directory) = 0;
    // Rename a single-file asset (material/postprocessing); newName is without the extension.
    virtual void contentDidRequestRename(Panda::AssetId assetId, const std::string &newName) = 0;
    // A new folder in the directory, named by the user.
    virtual void contentDidRequestNewFolder(const Panda::path_t &directory, const std::string &name) = 0;
    // A folder goes with everything in it; the user is asked first.
    virtual void contentDidRequestDeleteFolder(const Panda::path_t &folder) = 0;
};

// VIEW: the content browser — a view of the real file system <project>/Assets. The folder tree is
// on the left, breadcrumbs + the grid on the right (subfolders + all files of the folder, thumbnails
// for materials). The model, grid data and thumbnails (prefetch/cache) live in
// ContentBrowserDataSource; here are only widgets and intents.
class ContentBrowserView final : public EditorView {
public:
    explicit ContentBrowserView(ContentBrowserOutput *output);

    void refresh();             // rescan Assets (the current folder is preserved by path)
    void clearAssetSelection(); // clear the selection in the grid (e.g. when an entity is selected)
    std::vector<Panda::AssetId> selectedAssets() const; // the assets among the selected cells of the grid
    // Opens the folder of the asset and blinks its cell; the selection of the editor stays.
    void revealAsset(Panda::AssetId asset);
    // The folder the browser shows: where File → New puts an asset. Empty without a project.
    Panda::path_t currentDirectory();
    // An asset thumbnail is ready/updated: rebind its cell if it is visible in the current folder.
    void thumbnailDidBecomeReady(Panda::AssetId assetId);

private:
    using Folder = ContentBrowserDataSource::Folder;
    using FileEntry = ContentBrowserDataSource::FileEntry;

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool keyDown(PandaUI::KeyEvent &event) override;
    bool pointerDown(PandaUI::PointerEvent &event) override;

    // notify = false — a navigation the user did not make by hand (revealAsset): the selection of
    // the editor is not cleared.
    void setCurrentFolder(Folder *folder, bool notify = true);
    void emitSelection();
    void rebuildBreadcrumb();
    void updateStatus();
    void showGridContextMenu(int index, PandaUI::Point position);
    void presentMenu(std::shared_ptr<PandaUI::Menu> menu, PandaUI::Point position);

    ContentBrowserOutput *m_output;
    ContentBrowserDataSource m_dataSource; // FS model + grid data + thumbnails

    std::shared_ptr<PandaUI::TreeView> m_tree;
    std::shared_ptr<PandaUI::CollectionView> m_grid;
    std::shared_ptr<PandaUI::Panel> m_breadcrumb;
    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace Editor
