#include "UI/Panels/ContentBrowserView.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>

#include <Foundation/GraphicsAPI.hpp>

#include "UI/EditorDialogs.hpp"
#include "UI/EditorModelHelper.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "UI/Widgets/SplitView.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"

#include <Panda/GameLogic/GameContext.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    using Entry = ContentBrowserDataSource::Entry;
    using Folder = ContentBrowserDataSource::Folder;
    using FileEntry = ContentBrowserDataSource::FileEntry;

    // A row of a tree is an entry; the tree holds it as an untyped item.
    TreeItem toItem(Entry *entry) {
        return static_cast<void *>(entry);
    }
    Entry *toEntry(TreeItem item) {
        return static_cast<Entry *>(item);
    }
    Folder *toFolder(TreeItem item) {
        Entry *entry = toEntry(item);
        return entry && entry->isFolder ? static_cast<Folder *>(entry) : nullptr;
    }

    // The asset type string from the registry by id (for the icon/color).
    std::string assetTypeOf(Panda::AssetId id) {
        auto *handler = Panda::AssetHandlerEditor::current();
        if (!handler) { return "text"; }
        const auto &registry = handler->getRegistry();
        auto it = registry.find(id);
        return it != registry.end() ? assetTypeString(it->second.type) : "text";
    }

    // A kind the editor has a form for (EditorExtensions::assetKinds); the rest is text it does not
    // edit, and a double click hands it to the external editor.
    bool hasForm(const std::string &kind) {
        const AssetKindUi *ui = editorExtensions().assetKinds.find(kind);
        return ui != nullptr && ui->createEditor != nullptr;
    }

    // The look of a kind is its entry in the editor registry (EditorExtensions::assetKinds), for
    // the kinds of the core and of the modules alike; a file of no kind is a gray generic file.
    Color colorForType(const std::string &type, bool folder) {
        if (folder) { return Color(0xC9A45FFF); }
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(type)) { return ui->color; }
        return Color(0x8A8A8AFF);
    }

    constexpr float PREVIEW_SIDE = 62.f;   // side of the square preview slot in a grid cell
    constexpr float TOOLBAR_HEIGHT = 34.f;
    constexpr float SEARCH_WIDTH = 200.f;  // the search field of the icons (the list gives it the row)
    constexpr float TOOL_BUTTON = 26.f;

    const char *iconForType(const std::string &type, bool folder) {
        if (folder) { return Ic::Folder; }
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(type); ui && !ui->icon.empty()) {
            return ui->icon.c_str();
        }
        return Ic::FileGeneric;
    }

    std::shared_ptr<Label> makeNothingFound() {
        auto label = std::make_shared<Label>("Nothing found");
        label->setFont(Typo::Caption);
        label->setTextRole(TextRole::Dim);
        label->setTextAlignment(TextAlignment::Center);
        label->layout().setMargin(Edge::Top, 24.f);
        label->setCollapsed(true);
        return label;
    }

    class FolderCell final : public Panel {
    public:
        FolderCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(7.f);
            addSubview(makeIcon(Ic::Folder, Ic::RowSize, Color(0xC9A45FFF)));
            m_label = std::make_shared<Label>("");
            m_label->setFont(Typo::Body);
            addSubview(m_label);
        }
        void setText(const std::string &text) {
            m_label->setText(text);
        }

    private:
        std::shared_ptr<Label> m_label;
    };

    class AssetCell final : public Panel {
    public:
        AssetCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Column);
            layout().setAlignItems(Align::Center);
            layout().setJustifyContent(Justify::Start); // the previews of a row stand on one line
            layout().setGap(5.f);
            layout().setPadding(Edge::Horizontal, 4.f);
            layout().setPadding(Edge::Top, 4.f);

            m_preview = std::make_shared<Panel>();
            m_preview->layout().setSize({PREVIEW_SIDE, PREVIEW_SIDE}); // square: thumbnails are letterboxed
            m_preview->layout().setJustifyContent(Justify::Center);
            m_preview->layout().setAlignItems(Align::Center);
            m_preview->surface().setCornerRadius(Radius::md);
            m_preview->surface().setBorderWidth(1.f);
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_preview->addSubview(m_icon);
            addSubview(m_preview);

            m_label = std::make_shared<Label>("");
            m_label->setFont(Typo::Item);
            m_label->setTextRole(TextRole::Secondary);
            m_label->setTextAlignment(TextAlignment::Center);
            m_label->setNumberOfLines(2);
            m_label->setClipsToBounds(true);
            m_label->layout().setWidth(Length::percent(100.f));
            addSubview(m_label);
        }
        // thumbnail (a thumbnail from an FBO, flipped) replaces the type icon when the handle is valid;
        // an unregistered file gets a gray generic icon and a muted caption.
        // broken — the asset's files are incomplete or unreadable (the Output panel says what is
        // wrong): the frame of the preview turns red.
        void bind(const std::string &name, const std::string &type, bool folder, bool registered,
                  TextureHandle thumbnail, bool broken = false) {
            const bool typed = folder || registered;
            if (thumbnail.isValid()) {
                m_icon->setTexture(thumbnail);
                m_icon->setFlipVertical(Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft);
                m_icon->setTintColor(Color(0xFFFFFFFF));
                // The thumbnail is already square (fitted with margins) — it takes the whole slot.
                m_icon->layout().setSize({PREVIEW_SIDE, PREVIEW_SIDE});
            } else {
                m_icon->setTexture(icons().get(typed ? iconForType(type, folder) : Ic::FileGeneric));
                m_icon->setFlipVertical(false);
                m_icon->setTintColor(typed ? ThemeColor(colorForType(type, folder)) : ThemeColor::textDim);
                m_icon->layout().setSize(folder ? Size(52.f, 52.f) : Size(46.f, 46.f));
            }
            m_preview->setBackgroundColor(folder ? ThemeColor(Color(0x00000000)) : ThemeColor::input);
            m_preview->surface().setBorderColor(
                folder ? ThemeColor(Color(0x00000000)) : broken ? ThemeColor::danger : ThemeColor::borderMed
            );
            m_label->setText(name);
            m_label->setTextRole(typed ? TextRole::Secondary : TextRole::Muted);
        }

    private:
        std::shared_ptr<Panel> m_preview;
        std::shared_ptr<ImageView> m_icon;
        std::shared_ptr<Label> m_label;
    };

    class Crumb final : public Control {
    public:
        Crumb(const std::string &text, bool current, std::function<void()> onClick)
            : m_current(current)
            , m_onClick(std::move(onClick)) {
            setCursor(Cursor::PointingHand);
            setBackgroundColor(Color(0x00000000));
            layout().setAlignItems(Align::Center);
            layout().setPadding(Edge::Horizontal, 3.f);
            m_label = std::make_shared<Label>(text);
            m_label->setFont(Typo::Body);
            addSubview(m_label);
            applyStyle();
        }

    protected:
        void performPrimaryAction() override {
            if (m_onClick) { m_onClick(); }
        }
        void controlStateChanged(ControlState, ControlState) override {
            applyStyle();
        }
        void themeChanged() override {
            applyStyle();
        }

    private:
        void applyStyle() {
            const bool hot = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted);
            m_label->setTextRole((m_current || hot) ? TextRole::Primary : TextRole::Secondary);
        }
        bool m_current;
        std::shared_ptr<Label> m_label;
        std::function<void()> m_onClick;
    };

    // A row of the list: the icon of the kind, the name and — for a found entry — its folder.
    class ListCell final : public Panel {
    public:
        ListCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(6.f);
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setSize({Ic::RowSize + 2.f, Ic::RowSize + 2.f});
            m_icon->layout().setFlexShrink(0.f);
            addSubview(m_icon);
            m_name = std::make_shared<Label>("");
            m_name->setFont(Typo::Body);
            m_name->layout().setFlexShrink(1.f);
            addSubview(m_name);
            m_detail = std::make_shared<Label>("");
            m_detail->setFont(Typo::Caption);
            m_detail->setTextRole(TextRole::Dim);
            m_detail->layout().setFlexShrink(4.f); // the folder gives way first
            addSubview(m_detail);
        }
        // registered — a file the engine knows (an unknown one is muted, as in the grid); broken —
        // its files are incomplete or unreadable (the Output panel says what is wrong).
        void bind(const std::string &name, const char *icon, ThemeColor tint, bool registered, bool broken,
                  const std::string &detail) {
            m_icon->setTexture(icons().get(icon));
            m_icon->setTintColor(tint);
            m_name->setText(name);
            if (broken) {
                m_name->setTextColor(ThemeColor::danger);
            } else {
                m_name->setTextRole(registered ? TextRole::Primary : TextRole::Muted);
            }
            m_detail->setText(detail);
            m_detail->setCollapsed(detail.empty());
        }

    private:
        std::shared_ptr<ImageView> m_icon;
        std::shared_ptr<Label> m_name;
        std::shared_ptr<Label> m_detail;
    };

} // namespace

ContentBrowserView::ContentBrowserView(ContentBrowserOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

/* ------------------------------ The views ------------------------------ */

void ContentBrowserView::addSubviews() {
    addSubview(makeToolbar());

    // Icons: the folders | breadcrumbs (in the toolbar) + the grid.
    m_tree = std::make_shared<TreeView>();
    configureFolderTree();
    m_grid = std::make_shared<CollectionView>(
        std::make_shared<GridCollectionLayout>(Size(PREVIEW_SIDE + 22.f, 108.f), 4.f, 6.f)
    );
    configureGrid();
    m_gridNothing = makeNothingFound();
    auto gridColumn = std::make_shared<Panel>();
    gridColumn->setBackgroundColor(Color(0x00000000));
    gridColumn->layout().setFlexDirection(FlexDirection::Column);
    gridColumn->addSubview(m_gridNothing);
    gridColumn->addSubview(m_grid);
    m_split = std::make_shared<SplitView>(SplitOrientation::Horizontal);
    m_split->addPane(m_tree, 1.f);
    m_split->addPane(gridColumn, 3.f);
    addSubview(m_split);

    // List: every folder and file in one tree.
    m_list = std::make_shared<TreeView>();
    configureList();
    m_listNothing = makeNothingFound();
    m_listColumn = std::make_shared<Panel>();
    m_listColumn->setBackgroundColor(Color(0x00000000));
    m_listColumn->layout().setFlexDirection(FlexDirection::Column);
    m_listColumn->layout().setFlexGrow(1.f);
    m_listColumn->addSubview(m_listNothing);
    m_listColumn->addSubview(m_list);
    addSubview(m_listColumn);

    applyMode();
}

std::shared_ptr<View> ContentBrowserView::makeToolbar() {
    // [folders] [breadcrumbs ............] [search] [icons | list]
    auto bar = std::make_shared<Panel>();
    bar->setBackgroundColor(Color(0x00000000));
    bar->layout().setFlexDirection(FlexDirection::Row);
    bar->layout().setAlignItems(Align::Center);
    bar->layout().setHeight(Length::points(TOOLBAR_HEIGHT));
    bar->layout().setFlexShrink(0.f);
    bar->layout().setPadding(Edge::Horizontal, 6.f);
    bar->layout().setGap(4.f);

    m_foldersButton = std::make_shared<IconButton>(Ic::Sidebar, Ic::HeaderSize, TOOL_BUTTON);
    m_foldersButton->setAccessibilityId("showFolders");
    m_foldersButton->setTooltip("Folders");
    m_foldersButton->setOnClick([this] { setFoldersShown(!m_foldersShown); });
    bar->addSubview(m_foldersButton);

    m_breadcrumb = std::make_shared<Panel>();
    m_breadcrumb->setBackgroundColor(Color(0x00000000));
    m_breadcrumb->setClipsToBounds(true); // a deep path does not run under the search
    m_breadcrumb->layout().setFlexDirection(FlexDirection::Row);
    m_breadcrumb->layout().setAlignItems(Align::Center);
    m_breadcrumb->layout().setFlexGrow(1.f);
    m_breadcrumb->layout().setFlexShrink(1.f);
    m_breadcrumb->layout().setGap(2.f);
    m_breadcrumb->layout().setPadding(Edge::Horizontal, 4.f);
    bar->addSubview(m_breadcrumb);

    // The search of all of Assets by name: what it found replaces the folder in the grid (the
    // tree in the list); an empty field is the folder again.
    m_searchField = std::make_shared<TextField>();
    m_searchField->setAccessibilityId("search");
    m_searchField->setPlaceholder("Search assets…");
    m_searchField->setClearsOnEscape(true);
    m_searchField->layout().setFlexShrink(1.f);
    m_searchField->layout().setMinWidth(Length::points(90.f));
    m_searchField->setOnTextChanged([this](TextField &field) { setSearch(field.getText()); });
    // Enter goes to what was found: its first item is selected, the arrows walk the rest.
    m_searchField->setOnSubmit([this](TextField &) {
        if (!m_dataSource.isSearching() || m_dataSource.found().empty()) { return; }
        Context *context = getContext();
        if (m_mode == Mode::List) {
            m_list->setSelectedItem(toItem(m_dataSource.found().front()));
            if (context) { context->setFocusedView(m_list.get()); }
        } else {
            m_grid->setSelectedIndices({0});
            if (context) { context->setFocusedView(m_grid.get()); }
        }
        emitSelection();
    });
    bar->addSubview(m_searchField);

    m_iconsButton = std::make_shared<IconButton>(Ic::ViewGrid, Ic::HeaderSize, TOOL_BUTTON);
    m_iconsButton->setAccessibilityId("viewIcons");
    m_iconsButton->setTooltip("Icons");
    m_iconsButton->setOnClick([this] { setMode(Mode::Icons); });
    bar->addSubview(m_iconsButton);
    m_listButton = std::make_shared<IconButton>(Ic::ViewList, Ic::HeaderSize, TOOL_BUTTON);
    m_listButton->setAccessibilityId("viewList");
    m_listButton->setTooltip("List");
    m_listButton->setOnClick([this] { setMode(Mode::List); });
    bar->addSubview(m_listButton);
    return bar;
}

void ContentBrowserView::configureFolderTree() {
    m_tree->setAccessibilityId("folders"); // a folder is in the tree and in the grid: a UI test says which
    m_tree->setSelectionMode(TreeSelectionMode::Single);
    m_tree->setRowHeight(24.f); // the same row height as in Hierarchy
    TreeDataSource source;
    source.rootItem = [this]() -> TreeItem { return toItem(m_dataSource.root()); };
    source.numberOfChildren = [](TreeItem item) {
        const Folder *folder = toFolder(item);
        return folder ? static_cast<int>(folder->children.size()) : 0;
    };
    source.childAt = [](TreeItem item, int index) -> TreeItem {
        return toItem(toFolder(item)->children[static_cast<size_t>(index)].get());
    };
    source.cellForReuse = [] { return std::make_shared<FolderCell>(); };
    source.configureCell = [](View &cell, TreeItem item, int) {
        static_cast<FolderCell &>(cell).setText(toEntry(item)->name);
    };
    source.titleForItem = [](TreeItem item) { return toEntry(item)->name; };
    m_tree->setDataSource(source);
    m_tree->setExpanded(toItem(m_dataSource.root()), true);
    m_tree->setOnSelectionChanged([this](TreeView &tree) {
        Folder *folder = toFolder(tree.selectedItem());
        if (!folder) { return; }
        if (m_dataSource.isSearching()) { clearSearch(); } // a folder chosen ends the search: its content is asked for
        setCurrentFolder(folder);
    });
    m_tree->setOnContextMenu([this](TreeView &, TreeItem item, Point position) {
        if (item) { showContextMenu(toEntry(item), position); }
    });
}

void ContentBrowserView::configureGrid() {
    // The grid: the subfolders + files of the current folder, or what the search found (data and
    // thumbnails from ContentBrowserDataSource). A cell holds the preview and two lines of the
    // name: a file name is rarely one short word. The cell is little wider than the preview — a name
    // of some thirteen letters takes one line, a longer one goes on in the second — so the previews
    // stand close, as the tiles of one grid.
    m_grid->setAccessibilityId("files");
    m_grid->setSelectionMode(CollectionSelectionMode::Multiple);
    m_grid->layout().setFlexGrow(1.f);
    CollectionViewDataSource source;
    source.numberOfItems = [this] { return m_dataSource.itemCount(); };
    source.cellForReuse = [] { return std::make_shared<AssetCell>(); };
    source.configureCell = [this](View &cell, int index) {
        const Entry *entry = m_dataSource.entryAt(index);
        if (!entry) { return; }
        AssetCell &assetCell = static_cast<AssetCell &>(cell);
        // A found entry says where it is: the names alone may repeat across the folders.
        assetCell.setTooltip(m_dataSource.isSearching() ? folderOf(*entry) : std::string());
        if (entry->isFolder) {
            assetCell.bind(entry->name, "folder", true, true, TextureHandle());
            return;
        }
        const FileEntry &file = static_cast<const FileEntry &>(*entry);
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        const bool broken = file.assetId != 0 && handler && !handler->assetFilesExist(file.assetId);
        assetCell.bind(
            file.name, assetTypeOf(file.assetId), false, file.assetId != 0, m_dataSource.thumbnailFor(index), broken
        );
    };
    source.titleForItem = [this](int index) -> std::string {
        const Entry *entry = m_dataSource.entryAt(index);
        return entry ? entry->name : std::string();
    };
    source.prefetchItems = [this](const std::vector<int> &indices) { m_dataSource.prefetchItems(indices); };
    source.cancelPrefetching = [this](const std::vector<int> &indices) { m_dataSource.cancelPrefetching(indices); };
    // Drag source: a registered file, or a folder as what it holds (fillDragPayload).
    source.canDragItem = [this](int index) {
        if (m_dataSource.folderAt(index) != nullptr) { return true; }
        const FileEntry *file = m_dataSource.fileAt(index);
        return file && file->assetId != 0;
    };
    source.fillDragPayload = [this](DragPayload &payload, const std::vector<int> &indices) {
        std::vector<Entry *> entries;
        for (int index : indices) {
            if (Entry *entry = m_dataSource.entryAt(index)) { entries.push_back(entry); }
        }
        fillDragPayload(payload, entries);
    };
    m_grid->setDataSource(source);
    m_grid->setOnSelectionChanged([this](CollectionView &) { emitSelection(); });
    m_grid->setOnActivate([this](int index) { activate(m_dataSource.entryAt(index)); });
    m_grid->setOnContextMenu([this](CollectionView &, int index, Point position) {
        showContextMenu(m_dataSource.entryAt(index), position); // no cell — the background
    });
}

void ContentBrowserView::configureList() {
    m_list->setAccessibilityId("list");
    m_list->setSelectionMode(TreeSelectionMode::Multiple);
    m_list->setRowHeight(24.f); // the rows of the Hierarchy
    m_list->layout().setFlexGrow(1.f);
    TreeDataSource source;
    source.rootItem = [this]() -> TreeItem { return toItem(m_dataSource.root()); };
    source.numberOfChildren = [this](TreeItem item) -> int {
        const Folder *folder = toFolder(item);
        if (!folder) { return 0; }
        // What a search found is one flat set under the root, as in the Hierarchy.
        if (m_dataSource.isSearching()) {
            return folder == m_dataSource.root() ? static_cast<int>(m_dataSource.found().size()) : 0;
        }
        return m_dataSource.childCount(*folder);
    };
    source.childAt = [this](TreeItem item, int index) -> TreeItem {
        Folder *folder = toFolder(item);
        Entry *child = nullptr;
        if (m_dataSource.isSearching()) {
            const std::vector<Entry *> &found = m_dataSource.found();
            if (index >= 0 && index < static_cast<int>(found.size())) { child = found[static_cast<size_t>(index)]; }
        } else if (folder) {
            child = m_dataSource.childAt(*folder, index);
        }
        return toItem(child ? child : m_dataSource.root()); // the tree takes no null item
    };
    source.cellForReuse = [] { return std::make_shared<ListCell>(); };
    source.configureCell = [this](View &cell, TreeItem item, int) {
        const Entry *entry = toEntry(item);
        ListCell &row = static_cast<ListCell &>(cell);
        const std::string detail = m_dataSource.isSearching() && entry != m_dataSource.root() ? folderOf(*entry) : std::string();
        if (entry->isFolder) {
            row.bind(entry->name, Ic::Folder, ThemeColor(colorForType("folder", true)), true, false, detail);
            return;
        }
        const FileEntry &file = static_cast<const FileEntry &>(*entry);
        const bool registered = file.assetId != 0;
        const std::string type = registered ? assetTypeOf(file.assetId) : std::string();
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        const bool broken = registered && handler && !handler->assetFilesExist(file.assetId);
        row.bind(file.name, registered ? iconForType(type, false) : Ic::FileGeneric,
                 registered ? ThemeColor(colorForType(type, false)) : ThemeColor::textDim, registered, broken, detail);
    };
    source.titleForItem = [](TreeItem item) { return toEntry(item)->name; };
    source.fillDragPayload = [this](DragPayload &payload, const std::vector<TreeItem> &items) {
        std::vector<Entry *> entries;
        for (TreeItem item : items) { entries.push_back(toEntry(item)); }
        return fillDragPayload(payload, entries);
    };
    m_list->setDataSource(source);
    m_list->setExpanded(toItem(m_dataSource.root()), true);
    m_list->setOnSelectionChanged([this](TreeView &) { emitSelection(); });
    m_list->setOnItemActivated([this](TreeView &, TreeItem item) { activate(toEntry(item)); });
    m_list->setOnContextMenu([this](TreeView &, TreeItem item, Point position) {
        showContextMenu(item ? toEntry(item) : nullptr, position);
    });
}

/* ------------------------------ The mode ------------------------------ */

void ContentBrowserView::setMode(Mode mode) {
    if (mode == m_mode) { return; }
    // The selection goes along: the rows (the cells) of the same assets.
    const std::vector<Panda::AssetId> selected = selectedAssets();
    m_mode = mode;
    applyMode();
    if (m_mode == Mode::List) {
        std::vector<TreeItem> items;
        for (Panda::AssetId id : selected) {
            FileEntry *file = m_dataSource.findAsset(id);
            if (!file) { continue; }
            for (Folder *above = file->parent; above; above = above->parent) { m_list->setExpanded(toItem(above), true); }
            items.push_back(toItem(file));
        }
        m_list->setSelectedItems(items);
        if (!items.empty()) { m_list->revealItem(items.front()); }
    } else {
        std::vector<int> indices;
        for (Panda::AssetId id : selected) {
            const int index = m_dataSource.indexOfAsset(id);
            if (index >= 0) { indices.push_back(index); }
        }
        m_grid->setSelectedIndices(indices);
    }
}

void ContentBrowserView::setFoldersShown(bool shown) {
    m_foldersShown = shown;
    applyMode();
}

void ContentBrowserView::applyMode() {
    const bool list = m_mode == Mode::List;
    m_split->setCollapsed(list);
    m_listColumn->setCollapsed(!list);
    m_split->setPaneVisible(0, m_foldersShown);
    m_foldersButton->setCollapsed(list); // the list is the folders itself
    m_foldersButton->setActive(m_foldersShown);
    m_iconsButton->setActive(!list);
    m_listButton->setActive(list);
    // The list has no current folder: the search takes the row.
    m_breadcrumb->setCollapsed(list);
    m_searchField->layout().setFlexGrow(list ? 1.f : 0.f);
    m_searchField->layout().setWidth(list ? Length::autoValue() : Length::points(SEARCH_WIDTH));
    rebuildBreadcrumb();
    updateNothingFound();
    setNeedsLayout();
}

void ContentBrowserView::focusSearch() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    context->setFocusedView(m_searchField.get());
    m_searchField->selectAll();
}

bool ContentBrowserView::hasKeyboard() const {
    const Context *context = getContext();
    for (const View *view = context ? context->getFocusedView() : nullptr; view; view = view->getSuperview()) {
        if (view == this) { return true; }
    }
    return false;
}

/* ------------------------------ The data ------------------------------ */

void ContentBrowserView::refresh() {
    // Every entry is made anew: the trees let go of the old ones first and get back, by path, what
    // was open and selected in them.
    const TreeState folders = releaseTree(*m_tree);
    const TreeState list = releaseTree(*m_list);
    m_dataSource.rebuild();
    m_tree->reloadData();
    m_list->reloadData();
    restoreTree(*m_tree, folders);
    restoreTree(*m_list, list);
    Folder *root = m_dataSource.root();
    m_tree->setExpanded(toItem(root), true);
    m_list->setExpanded(toItem(root), true);
    for (Folder *node = m_dataSource.currentFolder(); node; node = node->parent) { m_tree->setExpanded(toItem(node), true); }
    m_tree->setSelectedItem(toItem(m_dataSource.currentFolder()));
    m_grid->clearSelection();
    m_grid->reloadData();
    m_grid->setContentOffset(Point(0.f, 0.f));
    rebuildBreadcrumb();
    updateNothingFound();
}

ContentBrowserView::TreeState ContentBrowserView::releaseTree(TreeView &tree) {
    TreeState state;
    for (TreeItem item : tree.selectedItems()) { state.selected.push_back(toEntry(item)->path); }
    tree.setSelectedItems({});
    std::vector<Folder *> pending = {m_dataSource.root()};
    while (!pending.empty()) {
        Folder *folder = pending.back();
        pending.pop_back();
        if (!folder) { continue; }
        if (tree.isExpanded(toItem(folder))) {
            state.expanded.push_back(folder->path);
            tree.setExpanded(toItem(folder), false);
        }
        for (const auto &child : folder->children) { pending.push_back(child.get()); }
    }
    return state;
}

void ContentBrowserView::restoreTree(TreeView &tree, const TreeState &state) {
    for (const Panda::path_t &path : state.expanded) {
        if (Folder *folder = m_dataSource.findFolder(path)) { tree.setExpanded(toItem(folder), true); }
    }
    std::vector<TreeItem> selected;
    for (const Panda::path_t &path : state.selected) {
        if (Entry *entry = m_dataSource.findEntry(path)) { selected.push_back(toItem(entry)); }
    }
    tree.setSelectedItems(selected);
}

void ContentBrowserView::setSearch(const std::string &text) {
    if (text == m_dataSource.search()) { return; }
    // The selected cells stay selected where the new set has them.
    const std::vector<Panda::AssetId> selected = m_mode == Mode::Icons ? selectedAssets() : std::vector<Panda::AssetId>();
    m_dataSource.setSearch(text);
    m_grid->reloadData();
    m_grid->setContentOffset(Point(0.f, 0.f));
    std::vector<int> indices;
    for (Panda::AssetId id : selected) {
        const int index = m_dataSource.indexOfAsset(id);
        if (index >= 0) { indices.push_back(index); }
    }
    m_grid->setSelectedIndices(indices);
    m_list->reloadData();
    m_list->setContentOffset(Point(0.f, 0.f));
    rebuildBreadcrumb();
    updateNothingFound();
}

void ContentBrowserView::clearSearch() {
    m_searchField->setText({});
    setSearch({});
}

void ContentBrowserView::revealAsset(Panda::AssetId asset) {
    FileEntry *file = m_dataSource.findAsset(asset);
    if (!file) { return; }
    const std::vector<Entry *> &found = m_dataSource.found();
    // A search that did not find it gives way: the asset is shown where it lives.
    if (m_dataSource.isSearching() && std::find(found.begin(), found.end(), file) == found.end()) { clearSearch(); }
    if (m_mode == Mode::List) {
        if (!m_dataSource.isSearching()) {
            for (Folder *above = file->parent; above; above = above->parent) { m_list->setExpanded(toItem(above), true); }
        }
        m_list->flashItem(toItem(file));
        return;
    }
    if (!m_dataSource.isSearching() && file->parent != m_dataSource.currentFolder()) { setCurrentFolder(file->parent, false); }
    m_grid->flashIndex(m_dataSource.indexOfAsset(asset));
}

void ContentBrowserView::setCurrentFolder(Folder *folder, bool notify) {
    if (!folder) { return; }
    m_dataSource.setCurrentFolder(folder);
    m_grid->clearSelection();
    m_grid->reloadData();
    m_grid->setContentOffset(Point(0.f, 0.f));
    for (Folder *node = folder; node; node = node->parent) { m_tree->setExpanded(toItem(node), true); }
    m_tree->setSelectedItem(toItem(folder));
    rebuildBreadcrumb();
    if (notify && m_output) { m_output->contentDidSelectAssets({}); } // navigation clears the file selection
}

void ContentBrowserView::activate(Entry *entry) {
    if (!entry) { return; }
    if (entry->isFolder) {
        Folder *folder = static_cast<Folder *>(entry);
        if (m_mode == Mode::List && !m_dataSource.isSearching()) {
            m_list->setExpanded(toItem(folder), !m_list->isExpanded(toItem(folder)));
            return;
        }
        // A found folder, or one of the grid: its content is what was asked for.
        if (m_dataSource.isSearching()) { clearSearch(); }
        if (m_mode == Mode::List) {
            for (Folder *node = folder; node; node = node->parent) { m_list->setExpanded(toItem(node), true); }
            m_list->setSelectedItem(toItem(folder));
            m_list->revealItem(toItem(folder));
            return;
        }
        setCurrentFolder(folder);
        return;
    }
    const FileEntry &file = static_cast<const FileEntry &>(*entry);
    if (file.assetId == 0 || !m_output) { return; }
    const std::string kind = assetTypeOf(file.assetId);
    // A world and a prefab are documents: each opens in a tab of its own.
    if (kind == "world" || kind == "prefab") {
        m_output->contentDidRequestOpenWorld(file.assetId);
    } else if (!hasForm(kind)) {
        m_output->contentDidRequestOpenExternally(file.assetId);
    }
}

void ContentBrowserView::emitSelection() {
    if (m_output) { m_output->contentDidSelectAssets(selectedAssets()); }
}

std::vector<Panda::AssetId> ContentBrowserView::selectedAssets() const {
    std::vector<Panda::AssetId> ids;
    const auto add = [&ids](const Entry *entry) {
        if (entry && !entry->isFolder) {
            const Panda::AssetId id = static_cast<const FileEntry *>(entry)->assetId;
            if (id != 0) { ids.push_back(id); }
        }
    };
    if (m_mode == Mode::List) {
        for (TreeItem item : m_list->selectedItems()) { add(toEntry(item)); }
    } else {
        for (int index : m_grid->selectedIndices()) { add(m_dataSource.entryAt(index)); }
    }
    return ids;
}

void ContentBrowserView::thumbnailDidBecomeReady(Panda::AssetId assetId) {
    const int index = m_dataSource.indexOfAsset(assetId);
    if (index >= 0) { m_grid->reconfigureItem(index); }
}

void ContentBrowserView::clearAssetSelection() {
    m_grid->clearSelection();
    m_list->setSelectedItems({});
}

Panda::path_t ContentBrowserView::currentDirectory() {
    if (m_mode == Mode::List) {
        // The folder of the selected row: the row itself, the folder of a file; else Assets.
        if (const Entry *entry = toEntry(m_list->selectedItem())) {
            if (entry->isFolder) { return entry->path; }
            if (entry->parent) { return entry->parent->path; }
        }
        return m_dataSource.root() ? m_dataSource.root()->path : Panda::path_t();
    }
    Folder *folder = m_dataSource.currentFolder();
    return folder ? folder->path : Panda::path_t();
}

bool ContentBrowserView::fillDragPayload(DragPayload &payload, const std::vector<Entry *> &entries) const {
    // A folder is dragged as what it holds — payload "folder" + the ids of every asset in it and in
    // the folders under it, in the order the browser shows them. Its type is its own, so only a
    // target that takes many assets answers to it (a list field of a script); a well and the
    // viewport do not.
    if (entries.size() == 1 && entries.front()->isFolder) {
        const Folder &folder = static_cast<const Folder &>(*entries.front());
        if (&folder == m_dataSource.root()) { return false; } // Assets itself is not a thing to drag
        const auto collect = [&payload](const auto &self, const Folder &from) -> void {
            for (const FileEntry &file : from.files) {
                if (file.assetId != 0) { payload.items.push_back(static_cast<std::uint64_t>(file.assetId)); }
            }
            for (const std::shared_ptr<Folder> &child : from.children) { self(self, *child); }
        };
        collect(collect, folder);
        payload.type = "folder";
        payload.ghostText = folder.name;
        return true;
    }
    // Registered files: "asset.<kind>" + their AssetIds (a selection of several kinds: "asset").
    std::string kind;
    bool uniform = true;
    std::string firstName;
    for (const Entry *entry : entries) {
        if (entry->isFolder) { continue; }
        const FileEntry &file = static_cast<const FileEntry &>(*entry);
        if (file.assetId == 0) { continue; }
        const std::string k = assetTypeOf(file.assetId);
        if (payload.items.empty()) {
            kind = k;
            firstName = file.name;
        } else if (k != kind) {
            uniform = false;
        }
        payload.items.push_back(static_cast<std::uint64_t>(file.assetId));
    }
    if (payload.items.empty()) { return false; }
    payload.type = (uniform && !kind.empty()) ? "asset." + kind : "asset";
    payload.ghostText = payload.items.size() == 1 ? firstName : std::to_string(payload.items.size()) + " items";
    return true;
}

std::string ContentBrowserView::folderOf(const Entry &entry) const {
    std::vector<const Folder *> chain;
    for (const Folder *node = entry.parent; node; node = node->parent) { chain.push_back(node); }
    std::string path;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) { path += (path.empty() ? "" : "/") + (*it)->name; }
    return path;
}

/* ------------------------------ Menus ------------------------------ */

void ContentBrowserView::presentMenu(std::shared_ptr<Menu> menu, Point position) {
    Context *context = getContext();
    if (!context) { return; }
    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

void ContentBrowserView::showContextMenu(Entry *entry, Point position) {
    auto menu = std::make_shared<Menu>();

    // What can be made in a directory: a folder (its name is asked where the menu was), then the
    // assets the editor and the packages make. `prefix` is "New " in a menu that holds other
    // things too.
    const auto addCreateItems = [this, position](Menu &target, const Panda::path_t &directory, const std::string &prefix) {
        target.addItem(prefix + "Folder", [this, position, directory] {
            showNameDialog(getContext(), position, "New Folder", "New Folder", [this, directory](const std::string &name) {
                if (m_output) { m_output->contentDidRequestNewFolder(directory, name); }
            });
        });
        if (!m_output) { return; }
        const auto actions = m_output->contentCreateActions(directory);
        if (!actions.empty()) { target.addSeparator(); }
        for (const CreateAction &action : actions) { target.addItem(prefix + action.title, action.run); }
    };
    if (entry == nullptr) { // the background: the folder shown (the folder of the selected row)
        const Panda::path_t directory = currentDirectory();
        if (directory.empty()) { return; } // no project open
        addCreateItems(*menu, directory, "New ");
        menu->addSeparator();
        menu->addItem("Reveal in File Manager", [directory] { Panda::SystemTools::revealInFileManager(directory); });
    } else if (entry->isFolder) {
        const Panda::path_t directory = entry->path;
        if (directory.empty()) { return; } // no project open
        const bool root = entry == m_dataSource.root();
        if (m_mode == Mode::Icons && !root) {
            menu->addItem("Open", [this, directory] {
                // By path: the tree of folders may have been rebuilt while the menu was open.
                if (Folder *target = m_dataSource.findFolder(directory)) { activate(target); }
            });
        }
        // What can be made inside it, without going in.
        auto create = std::make_shared<Menu>();
        addCreateItems(*create, directory, "");
        menu->addSubmenu("New", create);
        if (!root) {
            menu->addItem("Delete", [this, directory] {
                if (m_output) { m_output->contentDidRequestDeleteFolder(directory); }
            });
        }
        menu->addSeparator();
        menu->addItem("Reveal in File Manager", [directory] { Panda::SystemTools::revealInFileManager(directory); });
    } else {
        const FileEntry &file = static_cast<const FileEntry &>(*entry);
        if (file.assetId == 0) { return; } // a file of a format foreign to the engine — no operations
        std::vector<Panda::AssetId> targets = selectedAssets();
        if (std::find(targets.begin(), targets.end(), file.assetId) == targets.end()) { targets = {file.assetId}; }
        const std::string typeOfClicked = assetTypeOf(file.assetId);
        if (typeOfClicked == "world" || typeOfClicked == "prefab") { // make it current (same as a double click)
            const Panda::AssetId worldId = file.assetId;
            menu->addItem(typeOfClicked == "world" ? "Open World" : "Open Prefab", [this, worldId] {
                if (m_output) { m_output->contentDidRequestOpenWorld(worldId); }
            });
        }
        if (typeOfClicked == "material" || typeOfClicked == "postfx") { // single-file ones can be renamed
            const Panda::AssetId renameId = file.assetId;
            const std::string stem = file.path.stem().string();
            menu->addItem("Rename", [this, renameId, stem, position] {
                showNameDialog(getContext(), position, "Rename", stem, [this, renameId](const std::string &newName) {
                    if (m_output) { m_output->contentDidRequestRename(renameId, newName); }
                }, "Rename");
            });
        }
        if (typeOfClicked == "mesh" || typeOfClicked == "prefab") { // into the world on screen: entities of a model, an instance of a prefab
            std::vector<Panda::AssetId> meshes;
            for (Panda::AssetId targetId : targets) {
                if (assetTypeOf(targetId) == typeOfClicked) { meshes.push_back(targetId); }
            }
            menu->addItem(meshes.size() > 1 ? "Add to World (all)" : "Add to World", [this, meshes] {
                if (m_output) { m_output->contentDidRequestAddToWorld(meshes); }
            });
        }
        if (m_output && targets.size() == 1) { m_output->contentAddFileItems(*menu, targets.front()); }
        menu->addItem(targets.size() > 1 ? "Reimport Selected" : "Reimport", [this, targets] {
            if (m_output) { m_output->contentDidRequestImport(targets); }
        });
        // Confirmation is handled by the coordinator (one for all panels); the panel only sends
        // the intent.
        menu->addItem(targets.size() > 1 ? "Delete Selected" : "Delete", [this, targets] {
            if (m_output) { m_output->contentDidRequestDelete(targets); }
        });
    }
    presentMenu(menu, position);
}

/* ------------------------------ Breadcrumbs ------------------------------ */

void ContentBrowserView::rebuildBreadcrumb() {
    m_breadcrumb->removeAllSubviews();
    // The list shows where things are itself; a search is of all of Assets, not of the folder.
    if (m_mode == Mode::List || m_dataSource.isSearching()) { return; }
    std::vector<Folder *> chain;
    for (Folder *node = m_dataSource.currentFolder(); node; node = node->parent) { chain.push_back(node); }
    std::reverse(chain.begin(), chain.end());
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i > 0) { m_breadcrumb->addSubview(makeIcon(Ic::ChevronRight, 13.f, ThemeColor::textDim)); }
        Folder *node = chain[i];
        const bool current = (i + 1 == chain.size());
        m_breadcrumb->addSubview(std::make_shared<Crumb>(node->name, current, [this, node] { setCurrentFolder(node); }));
    }
}

void ContentBrowserView::updateNothingFound() {
    const bool nothing = m_dataSource.isSearching() && m_dataSource.found().empty();
    m_gridNothing->setCollapsed(!nothing);
    m_listNothing->setCollapsed(!nothing);
}

/* ------------------------------ Input ------------------------------ */

bool ContentBrowserView::pointerDown(PointerEvent &) {
    clearAssetSelection();
    if (m_output) { m_output->contentDidSelectAssets({}); }
    return true;
}

bool ContentBrowserView::keyDown(KeyEvent &event) {
    // Escape in the search field: the field takes its text back first (the folder again); Escape in
    // the empty field comes here and hands the keyboard back to the grid (the list).
    if (event.key == Key::Escape && m_searchField->isFocused()) {
        if (Context *context = getContext()) {
            context->setFocusedView(m_mode == Mode::List ? static_cast<View *>(m_list.get()) : m_grid.get());
        }
        return true;
    }
    // Enter is the double click of the keyboard: on the selected cell (row).
    if (event.key == Key::Enter || event.key == Key::KeypadEnter) {
        Entry *entry = nullptr;
        if (m_mode == Mode::List) {
            entry = toEntry(m_list->selectedItem());
        } else if (!m_grid->selectedIndices().empty()) {
            entry = m_dataSource.entryAt(m_grid->selectedIndices().front());
        }
        if (entry) {
            activate(entry);
            return true;
        }
    }
    return false;
}

void ContentBrowserView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void ContentBrowserView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
    if (m_tree) { m_tree->setBackgroundColor(ThemeColor::surface); }
    if (m_grid) { m_grid->setBackgroundColor(ThemeColor::windowBackground); }
    if (m_list) { m_list->setBackgroundColor(ThemeColor::windowBackground); }
}

} // namespace Editor
