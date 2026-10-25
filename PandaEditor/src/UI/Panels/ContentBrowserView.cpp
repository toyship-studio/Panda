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

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // The asset type string from the registry by id (for the icon/color).
    std::string assetTypeOf(Panda::AssetId id) {
        auto *handler = assetHandler();
        if (!handler) { return "text"; }
        const auto &registry = handler->getRegistry();
        auto it = registry.find(id);
        return it != registry.end() ? assetTypeString(it->second.type) : "text";
    }

    Color colorForType(const std::string &type, bool folder) {
        if (folder) { return Color(0xC9A45FFF); }
        if (type == "texture") { return Color(0x12B69DFF); }
        if (type == "material") { return Color(0x57B894FF); }
        if (type == "mesh") { return Color(0x8B7FD1FF); }
        if (type == "scene") { return Color(0x6FC1FFFF); }
        if (type == "shader") { return Color(0xC98B5FFF); }
        if (type == "postfx") { return Color(0xD16FA8FF); }
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(type)) { return ui->color; } // module kind
        return Color(0x8A8A8AFF);
    }

    constexpr float PREVIEW_SIDE = 62.f; // side of the square preview slot in a grid cell

    const char *iconForType(const std::string &type, bool folder) {
        if (folder) { return Ic::Folder; }
        if (type == "texture") { return Ic::FilePng; }
        if (type == "material") { return Ic::Material; }
        if (type == "mesh") { return Ic::Mesh; }
        if (type == "scene") { return Ic::AssetWorld; }
        if (type == "shader") { return Ic::FileCpp; }
        if (type == "postfx") { return Ic::Sprite; }
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(type); ui && !ui->icon.empty()) {
            return ui->icon.c_str(); // module kind
        }
        return Ic::FileGeneric;
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
            layout().setJustifyContent(Justify::Center);
            layout().setGap(6.f);
            layout().setPadding(Edge::Horizontal, 4.f);

            m_preview = std::make_shared<Panel>();
            m_preview->layout().setSize({PREVIEW_SIDE, PREVIEW_SIDE}); // square: thumbnails are letterboxed
            m_preview->layout().setJustifyContent(Justify::Center);
            m_preview->layout().setAlignItems(Align::Center);
            m_preview->surface().setCornerRadius(7.f);
            m_preview->surface().setBorderWidth(1.f);
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_preview->addSubview(m_icon);
            addSubview(m_preview);

            m_label = std::make_shared<Label>("");
            m_label->setFont(Typo::Meta);
            m_label->setTextRole(TextRole::Secondary);
            m_label->setTextAlignment(TextAlignment::Center);
            m_label->setClipsToBounds(true);
            m_label->layout().setWidth(Length::percent(100.f));
            addSubview(m_label);
        }
        // thumbnail (a thumbnail from an FBO, flipped) replaces the type icon when the handle is valid;
        // an unregistered file gets a gray generic icon and a muted caption.
        void bind(const std::string &name, const std::string &type, bool folder, bool registered,
                  TextureHandle thumbnail) {
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
            m_preview->surface().setBorderColor(folder ? ThemeColor(Color(0x00000000)) : ThemeColor::borderMed);
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

} // namespace

ContentBrowserView::ContentBrowserView(ContentBrowserOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ContentBrowserView::refresh() {
    m_dataSource.rebuild();
    m_tree->reloadData();
    for (Folder *node = m_dataSource.currentFolder(); node; node = node->parent) {
        m_tree->setExpanded(node, true);
    }
    m_tree->setSelectedItem(m_dataSource.currentFolder());
    m_grid->clearSelection();
    m_grid->reloadData();
    m_grid->setContentOffset(Point(0.f, 0.f));
    rebuildBreadcrumb();
    updateStatus();
}

void ContentBrowserView::addSubviews() {
    // --- Folder tree ---
    m_tree = std::make_shared<TreeView>();
    m_tree->setSelectionMode(TreeSelectionMode::Single);
    m_tree->setRowHeight(24.f); // the same row height as in Hierarchy
    TreeDataSource treeSource;
    treeSource.rootItem = [this]() -> TreeItem { return m_dataSource.root(); };
    treeSource.numberOfChildren = [](TreeItem item) { return static_cast<int>(static_cast<Folder *>(item)->children.size()); };
    treeSource.childAt = [](TreeItem item, int index) -> TreeItem {
        return static_cast<Folder *>(item)->children[index].get();
    };
    treeSource.cellForReuse = [] { return std::make_shared<FolderCell>(); };
    treeSource.configureCell = [](View &cell, TreeItem item, int) {
        static_cast<FolderCell &>(cell).setText(static_cast<Folder *>(item)->name);
    };
    treeSource.titleForItem = [](TreeItem item) { return static_cast<Folder *>(item)->name; };
    m_tree->setDataSource(treeSource);
    m_tree->setOnSelectionChanged([this](TreeView &tree) {
        if (TreeItem item = tree.selectedItem()) { setCurrentFolder(static_cast<Folder *>(item)); }
    });

    // --- Grid: subfolders + files of the current folder (data/thumbnails from ContentBrowserDataSource) ---
    m_grid = std::make_shared<CollectionView>(std::make_shared<GridCollectionLayout>(Size(96.f, 88.f), 12.f, 8.f));
    m_grid->setSelectionMode(CollectionSelectionMode::Multiple);
    m_grid->layout().setFlexGrow(1.f);
    CollectionViewDataSource gridSource;
    gridSource.numberOfItems = [this] { return m_dataSource.itemCount(); };
    gridSource.cellForReuse = [] { return std::make_shared<AssetCell>(); };
    gridSource.configureCell = [this](View &cell, int index) {
        if (Folder *folder = m_dataSource.folderAt(index)) {
            static_cast<AssetCell &>(cell).bind(folder->name, "folder", true, true, TextureHandle());
        } else if (const FileEntry *file = m_dataSource.fileAt(index)) {
            static_cast<AssetCell &>(cell).bind(
                file->name, assetTypeOf(file->assetId), false, file->assetId != 0,
                m_dataSource.thumbnailFor(index)
            );
        }
    };
    gridSource.prefetchItems = [this](const std::vector<int> &indices) { m_dataSource.prefetchItems(indices); };
    gridSource.cancelPrefetching = [this](const std::vector<int> &indices) {
        m_dataSource.cancelPrefetching(indices);
    };
    // Drag-source: registered files only. Payload "asset.<kind>" + AssetId.
    gridSource.canDragItem = [this](int index) {
        const FileEntry *file = m_dataSource.fileAt(index);
        return file && file->assetId != 0;
    };
    gridSource.fillDragPayload = [this](DragPayload &payload, const std::vector<int> &indices) {
        std::string kind;
        bool uniform = true;
        std::string firstName;
        for (int i : indices) {
            const FileEntry *file = m_dataSource.fileAt(i);
            if (!file || file->assetId == 0) { continue; }
            const std::string k = assetTypeOf(file->assetId);
            if (payload.items.empty()) {
                kind = k;
                firstName = file->name;
            } else if (k != kind) {
                uniform = false;
            }
            payload.items.push_back(static_cast<std::uint64_t>(file->assetId));
        }
        payload.type = (uniform && !kind.empty()) ? "asset." + kind : "asset";
        payload.ghostText = payload.items.size() == 1 ? firstName : std::to_string(payload.items.size()) + " items";
    };
    m_grid->setDataSource(gridSource);
    m_grid->setOnSelectionChanged([this](CollectionView &) {
        updateStatus();
        emitSelection();
    });
    // Double click: a folder — enter it; a world — open it; a new supported file — import it.
    m_grid->setOnActivate([this](int index) {
        if (Folder *folder = m_dataSource.folderAt(index)) {
            setCurrentFolder(folder);
            return;
        }
        const FileEntry *file = m_dataSource.fileAt(index);
        if (!file || !m_output) { return; }
        if (file->assetId != 0 && assetTypeOf(file->assetId) == "scene") {
            m_output->contentDidRequestOpenWorld(file->assetId);
        }
    });
    m_grid->setOnContextMenu([this](CollectionView &, int index, Point position) { showGridContextMenu(index, position); });

    // --- Breadcrumbs ---
    m_breadcrumb = std::make_shared<Panel>();
    m_breadcrumb->setBackgroundColor(Color(0x00000000));
    m_breadcrumb->layout().setFlexDirection(FlexDirection::Row);
    m_breadcrumb->layout().setAlignItems(Align::Center);
    m_breadcrumb->layout().setGap(2.f);
    m_breadcrumb->layout().setPadding(Edge::Horizontal, 10.f);
    m_breadcrumb->layout().setPadding(Edge::Vertical, 4.f);

    auto rightColumn = std::make_shared<Panel>();
    rightColumn->setBackgroundColor(Color(0x00000000));
    rightColumn->layout().setFlexDirection(FlexDirection::Column);
    rightColumn->addSubview(m_breadcrumb);
    rightColumn->addSubview(m_grid);

    auto split = std::make_shared<SplitView>(SplitOrientation::Horizontal);
    split->addPane(m_tree, 1.f);
    split->addPane(rightColumn, 3.f);
    addSubview(split);

    m_status = std::make_shared<Label>("");
    m_status->setFont(Typo::Caption);
    m_status->setTextRole(TextRole::Muted);
    m_status->layout().setPadding(Edge::Horizontal, 12.f);
    m_status->layout().setPadding(Edge::Vertical, 8.f);
    addSubview(m_status);

    rebuildBreadcrumb();
    updateStatus();
}

void ContentBrowserView::setCurrentFolder(Folder *folder) {
    if (!folder) { return; }
    m_dataSource.setCurrentFolder(folder);
    m_grid->clearSelection();
    m_grid->reloadData();
    m_grid->setContentOffset(Point(0.f, 0.f));
    for (Folder *node = folder; node; node = node->parent) { m_tree->setExpanded(node, true); }
    m_tree->setSelectedItem(folder);
    rebuildBreadcrumb();
    updateStatus();
    if (m_output) { m_output->contentDidSelectAssets({}); } // navigation clears the file selection
}

void ContentBrowserView::emitSelection() {
    if (m_output) { m_output->contentDidSelectAssets(selectedAssets()); }
}

std::vector<Panda::AssetId> ContentBrowserView::selectedAssets() const {
    std::vector<Panda::AssetId> ids;
    for (int index : m_grid->selectedIndices()) {
        const FileEntry *file = m_dataSource.fileAt(index);
        if (file && file->assetId != 0) { ids.push_back(file->assetId); }
    }
    return ids;
}

void ContentBrowserView::thumbnailDidBecomeReady(Panda::AssetId assetId) {
    const int index = m_dataSource.indexOfAsset(assetId);
    if (index >= 0) { m_grid->reconfigureItem(index); }
}

void ContentBrowserView::clearAssetSelection() {
    m_grid->clearSelection();
    updateStatus();
}

void ContentBrowserView::presentMenu(std::shared_ptr<Menu> menu, Point position) {
    Context *context = getContext();
    if (!context) { return; }
    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

void ContentBrowserView::showGridContextMenu(int index, Point position) {
    Folder *currentFolder = m_dataSource.currentFolder();
    if (!currentFolder) { return; }
    auto menu = std::make_shared<Menu>();

    if (Folder *folder = m_dataSource.folderAt(index)) { // subfolder
        menu->addItem("Open", [this, folder] { setCurrentFolder(folder); });
    } else if (const FileEntry *file = m_dataSource.fileAt(index)) {
        if (file->assetId == 0) { // a file of a format foreign to the engine — no operations
            return;
        } else { // a registered asset
            std::vector<Panda::AssetId> targets = selectedAssets();
            if (std::find(targets.begin(), targets.end(), file->assetId) == targets.end()) { targets = {file->assetId}; }
            const std::string name = file->name;
            const std::string typeOfClicked = assetTypeOf(file->assetId);
            if (typeOfClicked == "scene") { // world: make it current (same as a double click)
                const Panda::AssetId worldId = file->assetId;
                menu->addItem("Open World", [this, worldId] {
                    if (m_output) { m_output->contentDidRequestOpenWorld(worldId); }
                });
            }
            if (typeOfClicked == "material" || typeOfClicked == "postfx") { // single-file ones can be renamed
                const Panda::AssetId renameId = file->assetId;
                const std::string stem = file->path.stem().string();
                menu->addItem("Rename", [this, renameId, stem, position] {
                    showNameDialog(getContext(), position, "Rename", stem, [this, renameId](const std::string &newName) {
                        if (m_output) { m_output->contentDidRequestRename(renameId, newName); }
                    });
                });
            }
            if (assetTypeOf(file->assetId) == "mesh") { // mesh: an instance into the current world
                std::vector<Panda::AssetId> meshes;
                for (Panda::AssetId targetId : targets) {
                    if (assetTypeOf(targetId) == "mesh") { meshes.push_back(targetId); }
                }
                menu->addItem(meshes.size() > 1 ? "Add to Scene (all)" : "Add to Scene", [this, meshes] {
                    if (m_output) { m_output->contentDidRequestAddToScene(meshes); }
                });
            }
            menu->addItem(targets.size() > 1 ? "Reimport Selected" : "Reimport", [this, targets] {
                if (m_output) { m_output->contentDidRequestImport(targets); }
            });
            // Confirmation is handled by the coordinator (one for all panels); the panel only sends
            // the intent.
            menu->addItem(targets.size() > 1 ? "Delete Selected" : "Delete", [this, targets] {
                if (m_output) { m_output->contentDidRequestDelete(targets); }
            });
        }
    } else { // grid background: creating assets in the current folder + opening it in Finder
        const Panda::path_t directory = currentFolder->path;
        if (directory.empty()) { return; } // no project open
        menu->addItem("New Material", [this, directory] { // the shader is Unlit; changed in the inspector
            if (m_output) {
                m_output->contentDidRequestCreateMaterial(directory, builtinShaderOptions().front().first);
            }
        });
        menu->addItem("New Postprocessing", [this, directory] {
            if (m_output) { m_output->contentDidRequestCreatePostprocessing(directory); }
        });
        menu->addItem("New Postprocess Shader", [this, directory] {
            if (m_output) { m_output->contentDidRequestCreatePostprocessShader(directory); }
        });
        for (const AssetCreator &creator : editorExtensions().assetCreators) { // modules' "New …" items
            const AssetCreator *creatorPtr = &creator; // the registry does not change while the project is open
            menu->addItem(creator.title, [this, directory, creatorPtr] {
                if (m_output) { m_output->contentDidRequestCreateAsset(directory, *creatorPtr); }
            });
        }
        menu->addSeparator();
        menu->addItem("Reveal in Finder", [directory] { Panda::SystemTools::open(directory); });
    }
    presentMenu(menu, position);
}

bool ContentBrowserView::pointerDown(PointerEvent &) {
    m_grid->clearSelection();
    updateStatus();
    if (m_output) { m_output->contentDidSelectAssets({}); }
    return true;
}

void ContentBrowserView::rebuildBreadcrumb() {
    m_breadcrumb->removeAllSubviews();
    std::vector<Folder *> chain;
    for (Folder *node = m_dataSource.currentFolder(); node; node = node->parent) { chain.push_back(node); }
    std::reverse(chain.begin(), chain.end());
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i > 0) { m_breadcrumb->addSubview(makeIcon(Ic::ChevronRight, 13.f, ThemeColor::textDim)); }
        Folder *node = chain[i];
        const bool current = (i + 1 == chain.size());
        m_breadcrumb->addSubview(std::make_shared<Crumb>(node->name, current, [this, node] { setCurrentFolder(node); }));
    }
    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    m_breadcrumb->addSubview(spacer);
    auto refresh = std::make_shared<IconButton>(Ic::Reload, Ic::RowSize, 24.f);
    refresh->setCornerRadius(5.f);
    refresh->setTooltip("Refresh");
    refresh->setOnClick([this] {
        if (m_output) { m_output->contentDidRequestRefresh(); } // registry rescan + panel rebuild
    });
    m_breadcrumb->addSubview(refresh);
}

void ContentBrowserView::updateStatus() {
    std::vector<Folder *> chain;
    for (Folder *node = m_dataSource.currentFolder(); node; node = node->parent) { chain.push_back(node); }
    std::reverse(chain.begin(), chain.end());
    std::string path;
    for (size_t i = 0; i < chain.size(); ++i) { path += (i ? "/" : "") + chain[i]->name; }

    const int count = m_dataSource.itemCount();
    const std::vector<int> &selection = m_grid->selectedIndices();
    if (selection.size() == 1) {
        const int index = selection.front();
        std::string name;
        if (Folder *folder = m_dataSource.folderAt(index)) {
            name = folder->name;
        } else if (const FileEntry *file = m_dataSource.fileAt(index)) {
            name = file->name;
        }
        m_status->setText(path + "  -  " + name);
    } else if (selection.size() > 1) {
        m_status->setText(path + "  -  " + std::to_string(selection.size()) + " selected");
    } else {
        m_status->setText(path + "  -  " + std::to_string(count) + " items");
    }
}

bool ContentBrowserView::keyDown(KeyEvent &event) {
    if (event.key == Key::Enter || event.key == Key::KeypadEnter) {
        const std::vector<int> &selection = m_grid->selectedIndices();
        if (!selection.empty()) {
            if (Folder *folder = m_dataSource.folderAt(selection.front())) {
                setCurrentFolder(folder);
                return true;
            }
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
}

} // namespace Editor
