#include "UI/Panels/HierarchyPanel.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/ObjectIcons.hpp>

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Prefabs/PrefabComponent.hpp>
#include <Panda/Prefabs/Prefabs.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // TreeItem (void*) carries the engine UUID (+1, so that the synthetic "World" root with UUID 0
    // is NON-null — TreeView requires a non-null TreeItem). Real entities have UUID≠0, so there is
    // no collision with the root.
    TreeItem toItem(std::uint32_t uuid) {
        return reinterpret_cast<TreeItem>(static_cast<std::uintptr_t>(uuid) + 1);
    }
    std::uint32_t toUuid(TreeItem item) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(item) - 1);
    }

    // Content of a tree row: the entity type icon + a caption. Reused while scrolling.
    class EntityCell final : public Panel {
    public:
        EntityCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(6.f);
            // The icon of the entity, with the sign of an addition over its lower right corner.
            m_iconBox = std::make_shared<Panel>();
            m_iconBox->setBackgroundColor(Color(0x00000000));
            m_iconBox->setUserInteractionEnabled(false);
            m_iconBox->layout().setSize({Ic::RowSize, Ic::RowSize});
            m_iconBox->layout().setFlexShrink(0.f);
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setSize({Ic::RowSize, Ic::RowSize});
            m_iconBox->addSubview(m_icon);
            m_added = std::make_shared<ImageView>();
            m_added->setContentMode(ImageContentMode::Fit);
            m_added->setUserInteractionEnabled(false);
            m_added->setTexture(icons().get(Ic::Plus));
            m_added->setTintColor(ThemeColor::accentText);
            m_added->layout().setPositionType(PositionType::Absolute);
            m_added->layout().setPosition(Edge::Right, Length::points(-4.f));
            m_added->layout().setPosition(Edge::Bottom, Length::points(-3.f));
            m_added->layout().setSize({9.f, 9.f});
            m_added->setHidden(true);
            m_iconBox->addSubview(m_added);
            addSubview(m_iconBox);
            m_label = std::make_shared<Label>("");
            m_label->setFont(Typo::Body);
            addSubview(m_label);
        }
        void setText(const std::string &text) {
            m_label->setText(text);
        }
        // An empty name (the world root) — the icon is hidden, the row stays a plain caption.
        // prefab — the entity came from a prefab: its row is in the color of the accent, so the
        // part of the tree that is kept in another file reads apart from what the world holds.
        void setIcon(const char *name, bool prefab = false) {
            const bool hidden = name == nullptr;
            m_icon->setHidden(hidden);
            m_iconBox->layout().setDisplay(hidden ? Display::None : Display::Flex);
            if (!hidden) {
                m_icon->setTexture(icons().get(name));
                m_icon->setTintColor(prefab ? ThemeColor::accentText : ThemeColor::textMuted);
            }
            if (prefab) {
                m_label->setTextColor(ThemeColor::accentText);
            } else {
                m_label->setTextRole(TextRole::Primary);
            }
        }

        // added — the entity was added to an instance in the world (Prefabs::Addition): the
        // instance differs from its prefab by it, everything under it included.
        void setAdded(bool added) {
            m_added->setHidden(!added);
        }

    private:
        std::shared_ptr<Panel> m_iconBox;
        std::shared_ptr<ImageView> m_icon;
        std::shared_ptr<ImageView> m_added;
        std::shared_ptr<Label> m_label;
    };

} // namespace

HierarchyPanelView::HierarchyPanelView(HierarchyPanelOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void HierarchyPanelView::addSubviews() {
    // The search: typing shows the entities whose name has the text as a flat list; an empty
    // field is the tree again.
    m_searchField = std::make_shared<TextField>();
    m_searchField->setPlaceholder("Search entities…");
    m_searchField->setClearsOnEscape(true);
    m_searchField->layout().setMargin(Edge::Horizontal, 8.f);
    m_searchField->layout().setMargin(Edge::Top, 6.f);
    m_searchField->layout().setMargin(Edge::Bottom, 4.f);
    m_searchField->setOnTextChanged([this](TextField &field) {
        std::string text = field.getText();
        for (char &c : text) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (text == m_search) { return; }
        m_search = std::move(text);
        reload();
    });
    addSubview(m_searchField);

    m_tree = std::make_shared<TreeView>();
    m_tree->setBackgroundColor(Color(0x00000000));
    m_tree->layout().setFlexGrow(1.f);
    addSubview(m_tree);
    configureTree();

    // No world is open: the panel says so in place of the tree.
    m_empty = std::make_shared<Label>("No world is open");
    m_empty->setFont(Typo::Caption);
    m_empty->setTextRole(TextRole::Dim);
    m_empty->setTextAlignment(TextAlignment::Center);
    m_empty->layout().setFlexGrow(1.f);
    m_empty->layout().setMargin(Edge::Top, 24.f);
    m_empty->setCollapsed(true);
    addSubview(m_empty);
}

void HierarchyPanelView::configureTree() {
    m_tree->setSelectionMode(TreeSelectionMode::Multiple);

    TreeDataSource dataSource;
    // The top row: the synthetic root of a world (UUID 0) — or, for a prefab, its root entity:
    // a prefab is the tree of that entity, and the Hierarchy shows it as it is.
    dataSource.rootItem = [this] { return toItem(static_cast<std::uint32_t>(m_rootEntity)); };
    dataSource.numberOfChildren = [this](TreeItem item) -> int {
        if (!m_world) { return 0; }
        const std::uint32_t uuid = toUuid(item);
        if (uuid == static_cast<std::uint32_t>(m_rootEntity)) { return static_cast<int>(m_rootChildren.size()); }
        if (!m_search.empty()) { return 0; } // the matches are a flat list
        return static_cast<int>(childrenOf(uuid).size());
    };
    dataSource.childAt = [this](TreeItem item, int index) -> TreeItem {
        if (!m_world) { return toItem(0); }
        const std::uint32_t uuid = toUuid(item);
        const std::vector<std::uint32_t> &children = uuid == static_cast<std::uint32_t>(m_rootEntity) ? m_rootChildren : childrenOf(uuid);
        return index >= 0 && index < static_cast<int>(children.size()) ? toItem(children[static_cast<size_t>(index)]) : toItem(0);
    };
    dataSource.cellForReuse = [] { return std::make_shared<EntityCell>(); };
    dataSource.configureCell = [this](View &cell, TreeItem item, int) {
        EntityCell &entityCell = static_cast<EntityCell &>(cell);
        const std::uint32_t uuid = toUuid(item);
        if (uuid == 0 || !m_world) {
            entityCell.setText(m_rootTitle);
            entityCell.setIcon(nullptr); // the world root — no type icon
            entityCell.setAdded(false);
            return;
        }
        Panda::Entity entity = m_world->getById(Panda::UUID(uuid));
        entityCell.setText(entity.isValid() ? entity.getName() : m_rootTitle);
        // A part of an instance of a prefab; its root — and the root of a prefab nested in it —
        // carries the sign of a prefab.
        const bool part = Panda::Prefabs::instanceRoot(entity).isValid();
        const bool prefabRoot = part && entity.getComponent<Panda::PrefabComponent>().prefab != Panda::AssetId(0);
        entityCell.setIcon(entity.isValid() ? (prefabRoot ? Ic::Prefab : entityIconName(entity)) : nullptr, part);
        entityCell.setAdded(entity.isValid() && Panda::Prefabs::additionOf(entity).top == entity);
    };
    dataSource.titleForItem = [this](TreeItem item) -> std::string {
        const std::uint32_t uuid = toUuid(item);
        if (uuid == 0 || !m_world) { return m_rootTitle; }
        Panda::Entity entity = m_world->getById(Panda::UUID(uuid));
        return entity.isValid() ? entity.getName() : m_rootTitle;
    };
    // The id of a dragged entity = its UUID (the drop target in the inspector/viewport reads the
    // UUID from payload.items).
    dataSource.idForItem = [](TreeItem item) { return static_cast<std::uint64_t>(toUuid(item)); };
    m_tree->setDataSource(dataSource);
    m_tree->setDragType("entity");
    m_tree->setRowHeight(24.f);            // more compact than the default 30
    m_tree->setExpanded(toItem(0), true); // the "World" root (the only container) is always expanded

    m_tree->setOnSelectionChanged([this](TreeView &tree) {
        std::vector<Panda::UUID> ids;
        for (TreeItem item : tree.selectedItems()) {
            const std::uint32_t uuid = toUuid(item);
            if (uuid != 0) { ids.push_back(Panda::UUID(uuid)); }
        }
        m_output->hierarchyDidSelectEntities(ids);
    });
    m_tree->setOnMove([this](const std::vector<TreeItem> &items, TreeItem newParent) {
        std::vector<Panda::UUID> ids;
        for (TreeItem item : items) {
            const std::uint32_t uuid = toUuid(item);
            if (uuid != 0) { ids.push_back(Panda::UUID(uuid)); }
        }
        m_output->hierarchyDidReparent(ids, Panda::UUID(toUuid(newParent))); // newParent==0 → to the root of the world
    });
    m_tree->setOnContextMenu([this](TreeView &tree, TreeItem item, Point position) {
        showContextMenu(tree, Panda::UUID(toUuid(item)), position);
    });
    m_tree->setOnItemActivated([this](TreeView &, TreeItem item) {
        if (const std::uint32_t uuid = toUuid(item)) { m_output->hierarchyDidActivateEntity(Panda::UUID(uuid)); }
    });
}

void HierarchyPanelView::showContextMenu(TreeView &tree, Panda::UUID entityId, Point position) {
    Context *context = tree.getContext();
    if (!context) { return; }

    auto menu = std::make_shared<Menu>();
    // What can be created: for an entity — a child, for the background — at the root.
    const auto makeCreateMenu = [this, entityId] {
        auto create = std::make_shared<Menu>();
        for (const CreateAction &action : m_output->hierarchyCreateActions(entityId)) {
            create->addItem(action.title, action.run);
        }
        return create;
    };
    if (entityId != Panda::UUID(0)) {
        // The whole selection when the clicked row is a part of it, else the row alone.
        const auto clicked = [this, entityId] {
            std::vector<Panda::UUID> ids;
            for (TreeItem item : m_tree->selectedItems()) {
                if (const std::uint32_t uuid = toUuid(item)) { ids.push_back(Panda::UUID(uuid)); }
            }
            if (std::find(ids.begin(), ids.end(), entityId) == ids.end()) { ids = {entityId}; }
            return ids;
        };
        Panda::Entity entity = m_world != nullptr ? m_world->getById(entityId) : Panda::Entity();
        Panda::Entity instance = Panda::Prefabs::instanceRoot(entity);
        if (instance.isValid()) {
            menu->addItem("Open Prefab", [this, entityId] { m_output->hierarchyDidRequestOpenPrefab(entityId); });
            menu->addItem("Unpack Prefab", [this, clicked] { m_output->hierarchyDidRequestUnpack(clicked()); });
        }
        // An entity that is not a part of an instance, or the root of one (its prefab is then a
        // variant of that one).
        if (!instance.isValid() || instance == entity) {
            menu->addItem("Create Prefab", [this, entityId] { m_output->hierarchyDidRequestCreatePrefab(entityId); });
        }
        menu->addSeparator();
        menu->addSubmenu("Add Child", makeCreateMenu());
        menu->addItem("Rename", [this, entityId] { m_output->hierarchyDidRequestRename(entityId); });
        menu->addItem("Duplicate", [this, entityId] { m_output->hierarchyDidRequestDuplicate({entityId}); });
        menu->addItem("Copy", [this, entityId] {
            // The whole selection when the clicked row is a part of it, else the row alone.
            std::vector<Panda::UUID> ids;
            for (TreeItem item : m_tree->selectedItems()) {
                if (const std::uint32_t uuid = toUuid(item)) { ids.push_back(Panda::UUID(uuid)); }
            }
            if (std::find(ids.begin(), ids.end(), entityId) == ids.end()) { ids = {entityId}; }
            m_output->hierarchyDidRequestCopy(ids);
        });
        menu->addItem("Paste", [this] { m_output->hierarchyDidRequestPaste(); });
        menu->addSeparator();
        menu->addItem("Delete", [this, entityId] { m_output->hierarchyDidRequestDelete({entityId}); });
    } else {
        menu->addSubmenu("Create Entity", makeCreateMenu());
        menu->addItem("Paste", [this] { m_output->hierarchyDidRequestPaste(); });
    }

    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

bool HierarchyPanelView::pointerDown(PointerEvent &) {
    m_output->hierarchyDidSelectEntities({}); // click on the background → clear the selection
    return true;
}

// The keys of the entities (Delete, Duplicate, Copy, Cut, Paste, Rename) are commands of the
// editor (EditorController::registerCommands): they act on the selection with the keyboard in
// the Hierarchy or in the viewport alike. A key the tree does not use goes on to them.
bool HierarchyPanelView::keyDown(KeyEvent &event) {
    // Escape in the search field: the field takes its text back first (the whole tree again);
    // Escape in the empty field comes here and hands the keyboard back to the tree.
    if (event.key == Key::Escape && m_searchField->isFocused()) {
        if (Context *context = getContext()) { context->setFocusedView(m_tree.get()); }
        return true;
    }
    return false;
}

void HierarchyPanelView::focusSearch() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    context->setFocusedView(m_searchField.get());
    m_searchField->selectAll();
}

void HierarchyPanelView::setWorld(Panda::World *world) {
    m_world = world;
    // Without a world there is no tree to show and nothing to search.
    const bool none = world == nullptr;
    m_tree->setCollapsed(none);
    m_searchField->setCollapsed(none);
    m_empty->setCollapsed(!none);
    reload();
}

HierarchyState HierarchyPanelView::captureState() const {
    HierarchyState state;
    for (TreeItem item : m_tree->expandedItems()) {
        const std::uint32_t uuid = toUuid(item);
        if (uuid != 0) { state.expanded.push_back(Panda::UUID(uuid)); } // the root of the world is always open
    }
    state.scroll = m_tree->getContentOffset().y;
    state.search = m_searchField->getText();
    return state;
}

void HierarchyPanelView::restoreState(const HierarchyState &state) {
    std::vector<TreeItem> expanded{toItem(0)}; // the "World" root (the only container) is always expanded
    if (m_rootEntity != Panda::UUID(0)) { expanded.push_back(toItem(static_cast<std::uint32_t>(m_rootEntity))); }
    for (Panda::UUID id : state.expanded) { expanded.push_back(toItem(static_cast<std::uint32_t>(id))); }
    m_tree->setExpandedItems(expanded);
    m_searchField->setText(state.search); // a text set from here is not an edit: the search is set below
    std::string search = state.search;
    for (char &c : search) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    if (search != m_search) {
        m_search = std::move(search);
        reload();
    }
    m_tree->setContentOffset(Point(0.f, state.scroll));
}

void HierarchyPanelView::setRootEntity(Panda::UUID entity) {
    if (m_rootEntity == entity) { return; }
    m_rootEntity = entity;
    m_tree->setExpanded(toItem(static_cast<std::uint32_t>(entity)), true); // the top row is always open
    reload();
}

void HierarchyPanelView::setRootTitle(std::string title) {
    if (title.empty()) { title = "World"; }
    if (m_rootTitle == title) { return; }
    m_rootTitle = std::move(title);
    reload();
}

// The children of an entity in the order of the world (World::orderedChildren), read once per
// reload: the tree asks for them row by row.
const std::vector<std::uint32_t> &HierarchyPanelView::childrenOf(std::uint32_t uuid) {
    const auto cached = m_children.find(uuid);
    if (cached != m_children.end()) { return cached->second; }
    std::vector<std::uint32_t> &children = m_children[uuid];
    if (m_world) {
        for (Panda::UUID child : m_world->orderedChildren(Panda::UUID(uuid))) {
            children.push_back(static_cast<std::uint32_t>(child));
        }
    }
    return children;
}

void HierarchyPanelView::reload() {
    m_rootChildren.clear();
    m_children.clear();
    // A prefab: its one tree, from its root entity — the top row. What else its world holds
    // (nothing of the file) is not shown.
    Panda::Entity only = m_world != nullptr && m_rootEntity != Panda::UUID(0) ? m_world->getById(m_rootEntity) : Panda::Entity();
    if (m_world && m_search.empty()) {
        m_rootChildren = childrenOf(static_cast<std::uint32_t>(m_rootEntity));
    } else if (m_world) {
        std::vector<std::pair<std::string, std::uint32_t>> matches; // the search: lowercase name → id
        m_world->getAllEntitiesWith<Panda::IdComponent>().each([&](Panda::IdComponent &idc) {
            Panda::Entity entity = m_world->getById(idc.id);
            if (!entity.isValid()) { return; }
            if (only.isValid() && !only.isAncestorOf(entity)) { return; } // the root is the top row already
            std::string name = entity.getName();
            for (char &c : name) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (name.find(m_search) != std::string::npos) { matches.emplace_back(std::move(name), static_cast<std::uint32_t>(idc.id)); }
        });
        std::sort(matches.begin(), matches.end());
        for (const auto &match : matches) { m_rootChildren.push_back(match.second); }
    }
    m_tree->reloadData();
}

void HierarchyPanelView::setSelectedEntities(const std::vector<Panda::UUID> &entityIds) {
    std::vector<TreeItem> items;
    items.reserve(entityIds.size());
    for (Panda::UUID id : entityIds) { items.push_back(toItem(static_cast<std::uint32_t>(id))); }
    m_tree->setSelectedItems(items);
}

void HierarchyPanelView::expandEntity(Panda::UUID entityId, bool expanded) {
    m_tree->setExpanded(toItem(static_cast<std::uint32_t>(entityId)), expanded);
}

void HierarchyPanelView::revealEntity(Panda::UUID entityId) {
    if (!m_world) { return; }
    Panda::Entity entity = m_world->getById(entityId);
    if (!entity.isValid()) { return; }
    for (Panda::Entity above = entity.getParent(); above.isValid(); above = above.getParent()) {
        m_tree->setExpanded(toItem(static_cast<std::uint32_t>(above.getId())), true);
    }
    m_tree->revealItem(toItem(static_cast<std::uint32_t>(entityId)));
}

void HierarchyPanelView::flashEntity(Panda::UUID entityId) {
    revealEntity(entityId); // the ancestors open
    if (m_world && m_world->getById(entityId).isValid()) {
        m_tree->flashItem(toItem(static_cast<std::uint32_t>(entityId)));
    }
}

void HierarchyPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void HierarchyPanelView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
