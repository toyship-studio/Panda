#pragma once

#include "UI/Panels/ContentBrowserDataSource.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetCreator
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// Intents of the content browser (implemented by the coordinator). The identity of registered
// assets is the engine AssetId; new (not yet registered) files are addressed by paths.
class ContentBrowserOutput {
public:
    virtual ~ContentBrowserOutput() = default;
    virtual void contentDidSelectAssets(const std::vector<Panda::AssetId> &assets) = 0; // empty = clear selection
    virtual void contentDidRequestDelete(const std::vector<Panda::AssetId> &assets) = 0;
    virtual void contentDidRequestImport(const std::vector<Panda::AssetId> &assets) = 0; // reimport
    virtual void contentDidRequestRefresh() = 0; // Refresh: registry rescan + panel rebuild
    virtual void contentDidRequestAddToScene(const std::vector<Panda::AssetId> &assets) = 0; // mesh instance in world
    virtual void contentDidRequestOpenWorld(Panda::AssetId assetId) = 0; // open the .world as the current world
    // A new material in the directory (RMB on the grid background) with the chosen built-in shader.
    virtual void contentDidRequestCreateMaterial(const Panda::path_t &directory, Panda::AssetId shaderId) = 0;
    virtual void contentDidRequestCreatePostprocessing(const Panda::path_t &directory) = 0; // an empty .postfx
    // Rename a single-file asset (material/postprocessing); newName is without the extension.
    virtual void contentDidRequestRename(Panda::AssetId assetId, const std::string &newName) = 0;
    virtual void contentDidRequestCreatePostprocessShader(const Panda::path_t &directory) = 0; // a pair of templates
    // The "New …" of an editor part's contribution (EditorExtensions::assetCreators): the file is
    // written by the contribution, registered and selected by the coordinator.
    virtual void contentDidRequestCreateAsset(const Panda::path_t &directory, const AssetCreator &creator) = 0;
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

    void setCurrentFolder(Folder *folder);
    void emitSelection();
    std::vector<Panda::AssetId> selectedAssets() const;         // the registered ones from the selection
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
