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

    // The name a rename starts from: a folder's own, a file's without its extension (a shader
    // pair's without the compound one).
    std::string renameStem(const Panda::path_t &path, bool folder) {
        const std::string name = path.filename().string();
        return folder ? name : Panda::AssetHandlerEditor::splitName(name).first;
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
    // A browser narrower than this has no room for the folder tree beside the grid: a quarter of
    // it for the tree would leave a tree of cut names and a grid of one column.
    constexpr float NARROW_WIDTH = 420.f;
    // The rows of the list: of the Hierarchy by default; the largest holds a picture of 64 points —
    // a thumbnail at its own size on a 2x display (AssetPreviews::THUMBNAIL_SIZE).
    constexpr float LIST_ROW = 24.f;
    constexpr float LIST_ROW_MAX = 72.f;
    constexpr float LIST_PICTURES_FROM = 30.f; // rows this tall show the picture of an asset
    constexpr float LIST_ROW_STEPS[] = {24.f, 32.f, 44.f, 56.f, 72.f}; // Zoom In / Zoom Out

    const char *iconForType(const std::string &type, bool folder) {
        if (folder) { return Ic::Folder; }
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(type); ui && !ui->icon.empty()) {
            return ui->icon.c_str();
        }
        return Ic::FileGeneric;
    }

    bool isInside(const Panda::path_t &path, const Panda::path_t &folder) {
        const auto mismatch = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end());
        return mismatch.first == folder.end();
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
        Crumb(const std::string &text, Panda::path_t folder, bool current, std::function<void()> onClick)
            : m_folder(std::move(folder))
            , m_current(current)
            , m_onClick(std::move(onClick)) {
            setCursor(Cursor::PointingHand);
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setPadding(Edge::Horizontal, 3.f);
            // A path longer than its row gives way from the head: the folders above shorten to a
            // few letters ("Ass… › Show… › Materials"), the folder shown keeps its whole name.
            layout().setFlexShrink(current ? 0.f : 1.f);
            layout().setMinWidth(Length::points(current ? 0.f : 26.f));
            m_label = std::make_shared<Label>(text);
            m_label->setFont(Typo::Body);
            m_label->setTextOverflowMode(TextOverflowMode::TailEllipsis);
            m_label->layout().setFlexShrink(1.f);
            addSubview(m_label);
            if (!current) { setTooltip(text); } // a shortened name is read here
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

    public:
        const Panda::path_t &folder() const { // what a drop onto the crumb goes into
            return m_folder;
        }
        // A folder of the path is a button by its name, whether the name is shown whole or not.
        void describeAccessibility(Accessibility &accessibility) const override {
            accessibility.role = "button";
            accessibility.label = m_label->getText();
            accessibility.selected = m_current;
            accessibility.atomic = true;
        }

    private:
        void applyStyle() {
            const bool hot = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted);
            m_label->setTextRole((m_current || hot) ? TextRole::Primary : TextRole::Secondary);
        }
        Panda::path_t m_folder;
        bool m_current;
        std::shared_ptr<Label> m_label;
        std::function<void()> m_onClick;
    };

    // A row of the list: the icon of the kind — or, in a row tall enough, the picture of the
    // asset —, the name and — for a found entry — its folder.
    class ListCell final : public Panel {
    public:
        ListCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(6.f);
            // The slot is as wide as it is tall, so the names of a list stand on one line whatever
            // stands in it.
            m_slot = std::make_shared<Panel>();
            m_slot->setBackgroundColor(Color(0x00000000));
            m_slot->setUserInteractionEnabled(false);
            m_slot->setClipsToBounds(true);
            m_slot->layout().setJustifyContent(Justify::Center);
            m_slot->layout().setAlignItems(Align::Center);
            m_slot->layout().setFlexShrink(0.f);
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_slot->addSubview(m_icon);
            addSubview(m_slot);
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
        // its files are incomplete or unreadable (the Output panel says what is wrong). rowHeight —
        // of the list now; thumbnail — the picture of the asset, when the row is tall enough to
        // show one and the asset has it.
        void bind(const std::string &name, const char *icon, ThemeColor tint, bool registered, bool broken,
                  const std::string &detail, float rowHeight, TextureHandle thumbnail) {
            const float slot = std::max(Ic::RowSize + 2.f, rowHeight - 6.f);
            m_slot->layout().setSize({slot, slot});
            if (thumbnail.isValid()) {
                m_icon->setTexture(thumbnail);
                m_icon->setFlipVertical(Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft);
                m_icon->setTintColor(Color(0xFFFFFFFF));
                m_icon->layout().setSize({slot, slot});
                m_slot->surface().setCornerRadius(Radius::sm);
            } else {
                // The sign of a kind grows with the row, not as far as a picture would: it says
                // what the file is, it does not show it.
                const float sign = std::min(slot, Ic::RowSize + 2.f + (slot - Ic::RowSize - 2.f) * 0.5f);
                m_icon->setTexture(icons().get(icon));
                m_icon->setFlipVertical(false);
                m_icon->setTintColor(tint);
                m_icon->layout().setSize({sign, sign});
                m_slot->surface().setCornerRadius(0.f);
            }
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
        std::shared_ptr<Panel> m_slot;
        std::shared_ptr<ImageView> m_icon;
        std::shared_ptr<Label> m_name;
        std::shared_ptr<Label> m_detail;
    };

} // namespace

ContentBrowserView::ContentBrowserView(ContentBrowserOutput *output)
    : m_output(output)
    , m_listRowHeight(LIST_ROW) {
    addSubviews();
    setupLayout();
    setupStyle();
}

/* ------------------------------ The views ------------------------------ */

void ContentBrowserView::addSubviews() {
    addSubview(makeToolbar());
    // The path of a narrow browser: a row of its own under the toolbar (applyMode moves it there).
    m_pathRow = std::make_shared<Panel>();
    m_pathRow->setBackgroundColor(Color(0x00000000));
    m_pathRow->layout().setFlexDirection(FlexDirection::Row);
    m_pathRow->layout().setAlignItems(Align::Center);
    m_pathRow->layout().setHeight(Length::points(24.f));
    m_pathRow->layout().setFlexShrink(0.f);
    m_pathRow->layout().setPadding(Edge::Horizontal, 6.f);
    m_pathRow->setCollapsed(true);
    addSubview(m_pathRow);

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
    m_breadcrumb->setAccessibilityId("path"); // the folders above the one shown, for a UI test
    m_breadcrumb->setBackgroundColor(Color(0x00000000));
    m_breadcrumb->setClipsToBounds(true); // a deep path does not run under the search
    m_breadcrumb->layout().setFlexDirection(FlexDirection::Row);
    m_breadcrumb->layout().setAlignItems(Align::Center);
    m_breadcrumb->layout().setFlexGrow(1.f);
    m_breadcrumb->layout().setFlexShrink(1.f);
    m_breadcrumb->layout().setGap(2.f);
    m_breadcrumb->layout().setPadding(Edge::Horizontal, 4.f);
    // The path stands in the toolbar while the browser is wide, under it while it is narrow.
    m_crumbSlot = std::make_shared<Panel>();
    m_crumbSlot->setBackgroundColor(Color(0x00000000));
    m_crumbSlot->layout().setFlexDirection(FlexDirection::Row);
    m_crumbSlot->layout().setAlignItems(Align::Center);
    m_crumbSlot->layout().setFlexGrow(1.f);
    m_crumbSlot->layout().setFlexShrink(1.f);
    m_crumbSlot->addSubview(m_breadcrumb);
    bar->addSubview(m_crumbSlot);

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
    source.fillDragPayload = [this](DragPayload &payload, const std::vector<TreeItem> &items) {
        std::vector<Entry *> entries;
        for (TreeItem item : items) { entries.push_back(toEntry(item)); }
        return fillDragPayload(payload, entries);
    };
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
    m_list->setRowHeight(m_listRowHeight);
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
            row.bind(
                entry->name, Ic::Folder, ThemeColor(colorForType("folder", true)), true, false, detail, m_listRowHeight,
                TextureHandle()
            );
            return;
        }
        const FileEntry &file = static_cast<const FileEntry &>(*entry);
        const bool registered = file.assetId != 0;
        const std::string type = registered ? assetTypeOf(file.assetId) : std::string();
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        const bool broken = registered && handler && !handler->assetFilesExist(file.assetId);
        // Rows tall enough show what the asset looks like (asked for the rows on screen only: the
        // tree binds no others).
        const TextureHandle thumbnail =
            m_listRowHeight >= LIST_PICTURES_FROM ? m_dataSource.thumbnailOf(file) : TextureHandle();
        row.bind(file.name, registered ? iconForType(type, false) : Ic::FileGeneric,
                 registered ? ThemeColor(colorForType(type, false)) : ThemeColor::textDim, registered, broken, detail,
                 m_listRowHeight, thumbnail);
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
    // The folders stand beside the grid where there is room for both; a narrow browser shows the
    // grid alone and offers no button for what would not fit.
    m_split->setPaneVisible(0, m_foldersShown && !m_narrow);
    m_foldersButton->setCollapsed(list || m_narrow); // the list is the folders itself
    m_foldersButton->setActive(m_foldersShown);
    m_iconsButton->setActive(!list);
    m_listButton->setActive(list);
    // The path: in the toolbar of a wide browser, in a row under it in a narrow one. The list has
    // no current folder: the search takes the row of the toolbar, as it does where the path is below.
    const bool pathBelow = m_narrow && !list;
    Panel *home = pathBelow ? m_pathRow.get() : m_crumbSlot.get();
    if (m_breadcrumb->getSuperview() != home) { home->addSubview(m_breadcrumb); }
    m_breadcrumb->setCollapsed(list);
    m_crumbSlot->setCollapsed(list || m_narrow);
    m_pathRow->setCollapsed(!pathBelow);
    const bool wideSearch = list || m_narrow;
    m_searchField->layout().setFlexGrow(wideSearch ? 1.f : 0.f);
    m_searchField->layout().setWidth(wideSearch ? Length::autoValue() : Length::points(SEARCH_WIDTH));
    rebuildBreadcrumb();
    updateNothingFound();
    setNeedsLayout();
}

void ContentBrowserView::update(double deltaTime) {
    EditorView::update(deltaTime);
    const float width = getFrame().size.width;
    if (width <= 0.f) { return; } // not laid out, or its dock is hidden
    const bool narrow = width < NARROW_WIDTH;
    if (narrow == m_narrow) { return; }
    m_narrow = narrow;
    applyMode();
}

/* ------------------------------ The rows of the list ------------------------------ */

void ContentBrowserView::setListRowHeight(float height) {
    height = height <= 0.f ? LIST_ROW : std::clamp(height, LIST_ROW, LIST_ROW_MAX);
    if (height == m_listRowHeight) { return; }
    m_listRowHeight = height;
    m_list->setRowHeight(height); // the rows on screen are bound again, with or without their pictures
}

bool ContentBrowserView::zoomList(int step) {
    if (m_mode != Mode::List) { return false; }
    float target = m_listRowHeight;
    if (step > 0) {
        for (float size : LIST_ROW_STEPS) {
            if (size > m_listRowHeight + 0.5f) {
                target = size;
                break;
            }
        }
    } else {
        for (float size : LIST_ROW_STEPS) {
            if (size < m_listRowHeight - 0.5f) { target = size; }
        }
    }
    if (target == m_listRowHeight) { return false; }
    setListRowHeight(target);
    return true;
}

bool ContentBrowserView::scroll(ScrollEvent &event) {
    // A pinch over the list makes its rows larger or smaller, as it zooms what else can be looked
    // at closer; the list itself scrolls by everything else.
    if (event.magnification == 0.f || m_mode != Mode::List) { return false; }
    setListRowHeight(m_listRowHeight * (1.f + event.magnification));
    return true;
}

void ContentBrowserView::showRenameDialog(const Entry &entry, Point at) {
    const Panda::path_t path = entry.path;
    showNameDialog(getContext(), at, "Rename", renameStem(path, entry.isFolder), [this, path](const std::string &newName) {
        if (m_output) { m_output->contentDidRequestRename(path, newName); }
    }, "Rename");
}

ContentBrowserView::Folder *ContentBrowserView::selectedFolder() const {
    Entry *entry = nullptr;
    if (m_mode == Mode::List) {
        if (m_list->selectedItems().size() == 1) { entry = toEntry(m_list->selectedItems().front()); }
    } else if (m_grid->selectedIndices().size() == 1) {
        entry = m_dataSource.entryAt(m_grid->selectedIndices().front());
    }
    return entry && entry->isFolder && entry != m_dataSource.root() ? static_cast<Folder *>(entry) : nullptr;
}

bool ContentBrowserView::requestDeleteSelection() {
    if (!m_output) { return false; }
    const std::vector<Panda::AssetId> assets = selectedAssets();
    if (!assets.empty()) {
        m_output->contentDidRequestDelete(assets);
        return true;
    }
    if (Folder *folder = selectedFolder()) {
        m_output->contentDidRequestDeleteFolder(folder->path);
        return true;
    }
    return false;
}

bool ContentBrowserView::requestDuplicateSelection() {
    const std::vector<Panda::AssetId> assets = selectedAssets();
    if (assets.empty() || !m_output) { return false; }
    m_output->contentDidRequestDuplicate(assets);
    return true;
}

bool ContentBrowserView::requestRenameSelection() {
    // One asset, or one folder.
    Entry *entry = nullptr;
    const std::vector<Panda::AssetId> assets = selectedAssets();
    if (assets.size() == 1) {
        entry = m_dataSource.findAsset(assets.front());
    } else if (assets.empty()) {
        entry = selectedFolder();
    }
    if (entry == nullptr) { return false; }
    // The dialog opens under the cell (the row) of the entry, or at the top of the browser.
    Rect frame;
    if (m_mode == Mode::List) {
        frame = m_list->frameOfItem(toItem(entry));
    } else {
        frame = m_grid->frameOfIndex(entry->isFolder ? m_dataSource.indexOfFolder(entry->path)
                                                     : m_dataSource.indexOfAsset(static_cast<FileEntry *>(entry)->assetId));
    }
    if (frame.size.width <= 0.f) { frame = getWorldFrame(); }
    showRenameDialog(*entry, Point(frame.origin.x, frame.origin.y + frame.size.height));
    return true;
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
    if (m_output) { m_output->contentDidShowFolder(folder->path); }
    if (notify && m_output) { m_output->contentDidSelectAssets({}); } // navigation clears the file selection
}

void ContentBrowserView::showFolder(const Panda::path_t &folder) {
    if (Folder *found = m_dataSource.findFolder(folder)) { setCurrentFolder(found, false); }
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
    m_output->contentDidRequestOpen(file.assetId); // a document opens in its tab
}

void ContentBrowserView::emitSelection() {
    if (m_output) { m_output->contentDidSelectAssets(selectedAssets()); }
}

std::vector<Panda::AssetId> ContentBrowserView::selectedAssets() const {
    std::vector<Panda::AssetId> ids;
    const auto add = [&ids](const Entry *entry) {
        if (entry && !entry->isFolder) {
            const Panda::AssetId id = static_cast<const FileEntry *>(entry)->assetId;
            // Once: the two files of a shader pair are cells of one asset.
            if (id != 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) { ids.push_back(id); }
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
    if (m_mode == Mode::List && m_listRowHeight >= LIST_PICTURES_FROM) {
        if (FileEntry *file = m_dataSource.findAsset(assetId)) { m_list->reconfigureItem(toItem(file)); }
    }
}

void ContentBrowserView::showSelectedAssets(const std::vector<Panda::AssetId> &assets) {
    std::vector<int> indices;
    std::vector<TreeItem> rows;
    for (Panda::AssetId id : assets) {
        const int index = m_dataSource.indexOfAsset(id);
        if (index >= 0) { indices.push_back(index); }
        if (FileEntry *file = m_dataSource.findAsset(id)) { rows.push_back(toItem(file)); }
    }
    m_grid->setSelectedIndices(indices);
    m_list->setSelectedItems(rows);
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
        payload.files = {Panda::utf8Path(folder.path)};
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
        payload.files.push_back(Panda::utf8Path(file.path)); // a drop onto a folder of the browser moves them
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
        menu->addItem("Import Files", [this, directory] {
            if (m_output) { m_output->contentDidRequestImportDialog(directory); }
        });
        menu->addItem("Reveal in File Manager", [directory] { Panda::SystemTools::revealInFileManager(directory); });
        if (m_mode == Mode::List) { // the rows: larger ones show the pictures of the assets
            menu->addSeparator();
            if (m_listRowHeight < LIST_ROW_MAX - 0.5f) { menu->addItem("Larger Rows", [this] { zoomList(+1); }); }
            if (m_listRowHeight > LIST_ROW + 0.5f) { menu->addItem("Smaller Rows", [this] { zoomList(-1); }); }
        }
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
            menu->addItem("Rename", [this, directory, position] {
                if (Entry *target = m_dataSource.findEntry(directory)) { showRenameDialog(*target, position); }
            });
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
        {
            const Panda::AssetId clicked = file.assetId; // what a double click does: the document in its tab
            menu->addItem("Open", [this, clicked] {
                if (m_output) { m_output->contentDidRequestOpen(clicked); }
            });
        }
        {
            const Panda::path_t path = file.path;
            menu->addItem("Rename", [this, path, position] {
                // By path: the tree may have been rebuilt while the menu was open.
                if (Entry *target = m_dataSource.findEntry(path)) { showRenameDialog(*target, position); }
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
        menu->addItem(targets.size() > 1 ? "Duplicate Selected" : "Duplicate", [this, targets] {
            if (m_output) { m_output->contentDidRequestDuplicate(targets); }
        });
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
        if (i > 0) {
            auto chevron = makeIcon(Ic::ChevronRight, 13.f, ThemeColor::textDim);
            chevron->layout().setFlexShrink(0.f);
            m_breadcrumb->addSubview(chevron);
        }
        Folder *node = chain[i];
        const bool current = (i + 1 == chain.size());
        m_breadcrumb->addSubview(std::make_shared<Crumb>(node->name, node->path, current, [this, node] { setCurrentFolder(node); }));
    }
}

void ContentBrowserView::updateNothingFound() {
    const bool nothing = m_dataSource.isSearching() && m_dataSource.found().empty();
    m_gridNothing->setCollapsed(!nothing);
    m_listNothing->setCollapsed(!nothing);
}

/* ------------------------------ Input ------------------------------ */

ContentBrowserView::Folder *ContentBrowserView::folderAt(Point at) const {
    for (const std::shared_ptr<View> &view : m_breadcrumb->getSubviews()) {
        const auto *crumb = dynamic_cast<const Crumb *>(view.get());
        if (crumb && crumb->getWorldFrame().contains(at)) { return m_dataSource.findFolder(crumb->folder()); }
    }
    Entry *under = nullptr;
    if (m_mode == Mode::List) {
        under = toEntry(m_list->itemAt(at));
    } else if (m_foldersShown && m_tree->getWorldFrame().contains(at)) {
        under = toEntry(m_tree->itemAt(at));
    } else {
        under = m_dataSource.entryAt(m_grid->indexAt(at));
    }
    if (!under) { return nullptr; }
    return under->isFolder ? static_cast<Folder *>(under) : under->parent;
}

bool ContentBrowserView::isOwnDrag(const DragInfo &drag) const {
    const void *source = drag.payload.source;
    return (source == m_grid.get() || source == m_list.get() || source == m_tree.get()) && !drag.payload.files.empty();
}

bool ContentBrowserView::canMoveInto(const DragInfo &drag, const Folder *folder) const {
    if (folder == nullptr) { return false; }
    bool elsewhere = false;
    for (const std::string &file : drag.payload.files) {
        const Panda::path_t path = Panda::pathFromUtf8(file);
        if (isInside(folder->path, path)) { return false; } // a folder into itself, or into what is under it
        if (path.parent_path() != folder->path) { elsewhere = true; }
    }
    return elsewhere; // not where all of it is already
}

void ContentBrowserView::highlightDropFolder(const Folder *folder, Point at) {
    const int index = folder ? m_dataSource.indexOfFolder(folder->path) : -1;
    m_grid->setDropHighlightIndex(m_mode == Mode::Icons && m_grid->getWorldFrame().contains(at) ? index : -1);
    const bool overTree = m_mode == Mode::Icons && m_foldersShown && m_tree->getWorldFrame().contains(at);
    m_tree->setDropHighlightItem(overTree && folder ? toItem(const_cast<Folder *>(folder)) : nullptr);
    m_list->setDropHighlightItem(m_mode == Mode::List && folder ? toItem(const_cast<Folder *>(folder)) : nullptr);
}

bool ContentBrowserView::canAcceptDrop(const DragInfo &drag) {
    // Files of the file manager: into the folder under the point, or the folder shown.
    if (drag.payload.type == "files" && drag.payload.source == nullptr) {
        return !drag.payload.files.empty() && !currentDirectory().empty();
    }
    // What is dragged within the browser: into a folder under the point — a move.
    return isOwnDrag(drag) && canMoveInto(drag, folderAt(drag.windowPosition));
}

void ContentBrowserView::dragEntered(const DragInfo &drag) {
    dragUpdated(drag);
}

void ContentBrowserView::dragUpdated(const DragInfo &drag) {
    Folder *folder = folderAt(drag.windowPosition);
    highlightDropFolder(canMoveInto(drag, folder) ? folder : nullptr, drag.windowPosition);
}

void ContentBrowserView::dragExited() {
    highlightDropFolder(nullptr, Point());
}

bool ContentBrowserView::performDrop(const DragInfo &drag) {
    highlightDropFolder(nullptr, Point());
    std::vector<Panda::path_t> paths;
    paths.reserve(drag.payload.files.size());
    for (const std::string &file : drag.payload.files) { paths.push_back(Panda::pathFromUtf8(file)); }
    Folder *folder = folderAt(drag.windowPosition);
    if (drag.payload.type == "files" && drag.payload.source == nullptr) {
        const Panda::path_t directory = folder ? folder->path : currentDirectory();
        if (m_output) { m_output->contentDidRequestImportFiles(paths, directory); }
        return true;
    }
    if (!isOwnDrag(drag) || !canMoveInto(drag, folder)) { return false; }
    // The list opens the folder, so the moved rows are seen there (VS Code); the icons stay in the
    // folder they show, and the moved cells leave it (Finder, Unity).
    if (m_mode == Mode::List) { m_list->setExpanded(toItem(folder), true); }
    if (m_output) { m_output->contentDidRequestMove(paths, folder->path); }
    return true;
}

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
