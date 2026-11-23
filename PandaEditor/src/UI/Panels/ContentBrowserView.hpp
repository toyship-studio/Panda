#pragma once

#include "UI/CreateAction.hpp"
#include "UI/Panels/ContentBrowserDataSource.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetCreator
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

class IconButton;
class SplitView;

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
    virtual void contentDidRequestAddToWorld(const std::vector<Panda::AssetId> &assets) = 0; // a model becomes entities
    virtual void contentDidRequestOpenWorld(Panda::AssetId assetId) = 0; // open the .world as the current world
    // A double click on an asset the editor has no form for (a shader): its file goes to the
    // external editor of the settings — the editor has no text editor of its own.
    virtual void contentDidRequestOpenExternally(Panda::AssetId assetId) = 0;
    // Files of the file manager dropped onto the browser go into the directory (a folder with what
    // is in it), and become assets.
    virtual void contentDidRequestImportFiles(const std::vector<Panda::path_t> &files, const Panda::path_t &directory) = 0;
    // The same for the files the user chooses in the dialog of the system (Import Files).
    virtual void contentDidRequestImportDialog(const Panda::path_t &directory) = 0;
    // Copies of the assets next to them: a free name ("red 2.mat"), a new id, the settings of the
    // sidecar (the import of a texture, the materials of a model); selected.
    virtual void contentDidRequestDuplicate(const std::vector<Panda::AssetId> &assets) = 0;
    // The browser shows another folder (the session of the project remembers it).
    virtual void contentDidShowFolder(const Panda::path_t &folder) = 0;
    // The assets that can be created in the directory (RMB on the grid background): the same list
    // as in File → New.
    virtual CreateActions contentCreateActions(const Panda::path_t &directory) = 0;
    // Rename a file of an asset (with the files of its pair) or a folder; newName is without the
    // extension, which stays.
    virtual void contentDidRequestRename(const Panda::path_t &path, const std::string &newName) = 0;
    // Move files of assets and folders into the directory (a drag onto a folder).
    virtual void contentDidRequestMove(const std::vector<Panda::path_t> &paths, const Panda::path_t &directory) = 0;
    // A new folder in the directory, named by the user.
    virtual void contentDidRequestNewFolder(const Panda::path_t &directory, const std::string &name) = 0;
    // A folder goes with everything in it; the user is asked first.
    virtual void contentDidRequestDeleteFolder(const Panda::path_t &folder) = 0;
};

// VIEW: the content browser — a view of the real file system <project>/Assets, in one of two
// modes, Finder's "as Icons" and "as List" (the buttons at the right of its toolbar):
// - Icons: the folder tree on the left — the button at the left of the toolbar hides it —,
//   breadcrumbs and the grid of the current folder on the right (subfolders + all files, the
//   thumbnails of the kinds that have them);
// - List: one tree of every folder and file, the project panel of VS Code and Zed.
// The search of the toolbar looks through all of Assets by name: the grid (or the list) shows what
// it found as one flat set, folders first; the folder of each is its tooltip (a detail of the
// row). The model, the grid data and the thumbnails live in ContentBrowserDataSource; here are
// only widgets and intents.
class ContentBrowserView final : public EditorView {
public:
    enum class Mode { Icons, List };

    explicit ContentBrowserView(ContentBrowserOutput *output);

    void refresh();             // rescan Assets (the current folder, open folders and rows are kept by path)
    void clearAssetSelection(); // clear the selection of the cells and rows (e.g. when an entity is selected)
    // The cells (rows) of the assets the editor selected elsewhere are highlighted where the browser
    // shows them; nothing is announced back.
    void showSelectedAssets(const std::vector<Panda::AssetId> &assets);
    std::vector<Panda::AssetId> selectedAssets() const; // the assets among the selected cells (rows)
    // Opens the folder of the asset and blinks its cell (its row); the selection of the editor stays.
    void revealAsset(Panda::AssetId asset);
    // The folder File → New puts an asset in: the folder shown (Icons), the folder of the selected
    // row (List). Empty without a project.
    Panda::path_t currentDirectory();
    // Shows the folder of the path, when it is there (the folder of the last session).
    void showFolder(const Panda::path_t &folder);
    // An asset thumbnail is ready/updated: rebind its cell if the grid shows it.
    void thumbnailDidBecomeReady(Panda::AssetId assetId);

    // How the browser shows Assets; remembered with the layout of the editor (DockLayoutState).
    Mode mode() const {
        return m_mode;
    }
    void setMode(Mode mode);
    bool foldersShown() const {
        return m_foldersShown;
    }
    void setFoldersShown(bool shown); // the folder tree of the icons
    // The search field takes the keyboard (the Find command, with the keyboard in the browser).
    void focusSearch();
    // The keys of the browser (EditorController: Delete, F2): what is selected goes — the user is
    // asked first — or is renamed in the dialog at its cell. false — nothing selected can be.
    bool requestDeleteSelection();
    bool requestRenameSelection();
    bool requestDuplicateSelection(); // ⌘D: copies of the selected assets
    bool hasKeyboard() const; // the focus is in the browser

private:
    using Entry = ContentBrowserDataSource::Entry;
    using Folder = ContentBrowserDataSource::Folder;
    using FileEntry = ContentBrowserDataSource::FileEntry;

    // What a tree had open and selected, by path: its rows are entries, which a rescan makes anew.
    struct TreeState {
        std::vector<Panda::path_t> expanded;
        std::vector<Panda::path_t> selected;
    };

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool keyDown(PandaUI::KeyEvent &event) override;
    bool pointerDown(PandaUI::PointerEvent &event) override;
    // Drops: files of the file manager (Context::dropFiles) go into the folder under the point — a
    // folder cell of the grid, a folder row (the folder of a file row) of a tree, a crumb of the
    // path — or the folder shown; what is dragged within the browser moves into a folder under the
    // point, which wears the drop border meanwhile.
    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragUpdated(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;

    std::shared_ptr<PandaUI::View> makeToolbar();
    void configureFolderTree();
    void configureGrid();
    void configureList();

    void applyMode(); // what the mode and the folders button show
    void setSearch(const std::string &text);
    void clearSearch();
    // notify = false — a navigation the user did not make by hand (revealAsset): the selection of
    // the editor is not cleared.
    void setCurrentFolder(Folder *folder, bool notify = true);
    // A double click, Enter: a folder opens (Icons) or opens and closes (List), a world or a prefab
    // opens in its tab, a file the editor has no form for goes to the external editor.
    void activate(Entry *entry);
    void emitSelection();
    void rebuildBreadcrumb();
    void updateNothingFound();
    void showContextMenu(Entry *entry, PandaUI::Point position); // null — the background
    void presentMenu(std::shared_ptr<PandaUI::Menu> menu, PandaUI::Point position);
    // The payload of dragged entries: one folder — "folder" with the ids of every asset in it and
    // under it; files — "asset.<kind>" ("asset" for several kinds). False — nothing to drag.
    bool fillDragPayload(PandaUI::DragPayload &payload, const std::vector<Entry *> &entries) const;
    std::string folderOf(const Entry &entry) const; // "Assets/Textures": where a found entry is
    Folder *selectedFolder() const; // the one folder selected (not Assets itself); null — none, or more
    Folder *folderAt(PandaUI::Point at) const; // the folder a drop at the point goes into; null — none
    bool isOwnDrag(const PandaUI::DragInfo &drag) const; // dragged from a view of this browser
    bool canMoveInto(const PandaUI::DragInfo &drag, const Folder *folder) const;
    void highlightDropFolder(const Folder *folder, PandaUI::Point at);
    void showRenameDialog(const Entry &entry, PandaUI::Point at); // an asset (with its pair) or a folder
    TreeState releaseTree(PandaUI::TreeView &tree);
    void restoreTree(PandaUI::TreeView &tree, const TreeState &state);

    ContentBrowserOutput *m_output;
    ContentBrowserDataSource m_dataSource; // FS model + grid data + thumbnails
    Mode m_mode = Mode::Icons;
    bool m_foldersShown = true;

    std::shared_ptr<IconButton> m_foldersButton;
    std::shared_ptr<PandaUI::Panel> m_breadcrumb;
    std::shared_ptr<PandaUI::TextField> m_searchField;
    std::shared_ptr<IconButton> m_iconsButton;
    std::shared_ptr<IconButton> m_listButton;
    std::shared_ptr<SplitView> m_split;          // Icons: the folders | the grid
    std::shared_ptr<PandaUI::TreeView> m_tree;    // the folders
    std::shared_ptr<PandaUI::CollectionView> m_grid;
    std::shared_ptr<PandaUI::Label> m_gridNothing; // the search found nothing
    std::shared_ptr<PandaUI::Panel> m_listColumn; // List
    std::shared_ptr<PandaUI::TreeView> m_list;
    std::shared_ptr<PandaUI::Label> m_listNothing;
};

} // namespace Editor
