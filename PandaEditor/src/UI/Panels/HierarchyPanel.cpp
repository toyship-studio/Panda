#include "UI/Panels/HierarchyPanel.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include "UI/Widgets/ObjectIcons.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>

#include <cstdint>
#include <memory>
#include <string>
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
            m_icon = std::make_shared<ImageView>();
            m_icon->setContentMode(ImageContentMode::Fit);
            m_icon->setUserInteractionEnabled(false);
            m_icon->layout().setSize({Ic::RowSize, Ic::RowSize});
            m_icon->layout().setFlexShrink(0.f);
            addSubview(m_icon);
            m_label = std::make_shared<Label>("");
            m_label->setFont(Typo::Body);
            addSubview(m_label);
        }
        void setText(const std::string &text) {
            m_label->setText(text);
        }
        // An empty name (the world root) — the icon is hidden, the row stays a plain caption.
        void setIcon(const char *name) {
            const bool hidden = name == nullptr;
            m_icon->setHidden(hidden);
            m_icon->layout().setDisplay(hidden ? Display::None : Display::Flex);
            if (!hidden) {
                m_icon->setTexture(icons().get(name));
                m_icon->setTintColor(ThemeColor::textMuted);
            }
        }

    private:
        std::shared_ptr<ImageView> m_icon;
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
    m_tree = std::make_shared<TreeView>();
    m_tree->setBackgroundColor(Color(0x00000000));
    m_tree->layout().setFlexGrow(1.f);
    addSubview(m_tree);
    configureTree();

    m_status = std::make_shared<Label>("Selected: -");
    m_status->setFont(Typo::Caption);
    m_status->setTextRole(TextRole::Muted);
    m_status->layout().setPadding(Edge::Horizontal, 12.f);
    m_status->layout().setPadding(Edge::Vertical, 8.f);
    addSubview(m_status);
}

void HierarchyPanelView::configureTree() {
    m_tree->setSelectionMode(TreeSelectionMode::Multiple);

    TreeDataSource dataSource;
    dataSource.rootItem = [] { return toItem(0); }; // synthetic "World" root (UUID 0)
    dataSource.numberOfChildren = [this](TreeItem item) -> int {
        if (!m_world) { return 0; }
        const std::uint32_t uuid = toUuid(item);
        if (uuid == 0) { return static_cast<int>(m_rootChildren.size()); }
        Panda::Entity entity = m_world->getById(Panda::UUID(uuid));
        return entity.isValid() ? static_cast<int>(entity.getChildEntities().size()) : 0;
    };
    dataSource.childAt = [this](TreeItem item, int index) -> TreeItem {
        if (!m_world) { return toItem(0); }
        const std::uint32_t uuid = toUuid(item);
        if (uuid == 0) { return toItem(m_rootChildren[index]); }
        Panda::Entity entity = m_world->getById(Panda::UUID(uuid));
        return toItem(static_cast<std::uint32_t>(entity.getChildEntities()[index]));
    };
    dataSource.cellForReuse = [] { return std::make_shared<EntityCell>(); };
    dataSource.configureCell = [this](View &cell, TreeItem item, int) {
        EntityCell &entityCell = static_cast<EntityCell &>(cell);
        const std::uint32_t uuid = toUuid(item);
        if (uuid == 0 || !m_world) {
            entityCell.setText(m_rootTitle);
            entityCell.setIcon(nullptr); // the world root — no type icon
            return;
        }
        Panda::Entity entity = m_world->getById(Panda::UUID(uuid));
        entityCell.setText(entity.isValid() ? entity.getName() : m_rootTitle);
        entityCell.setIcon(entity.isValid() ? entityIconName(entity) : nullptr);
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
        m_output->hierarchyDidReparent(ids, Panda::UUID(toUuid(newParent))); // newParent==0 → to the scene root
    });
    m_tree->setOnContextMenu([this](TreeView &tree, TreeItem item, Point position) {
        showContextMenu(tree, Panda::UUID(toUuid(item)), position);
    });
}

void HierarchyPanelView::showContextMenu(TreeView &tree, Panda::UUID entityId, Point position) {
    Context *context = tree.getContext();
    if (!context) { return; }

    auto menu = std::make_shared<Menu>();
    // Create submenu: Empty Entity + primitives. For an entity — creating a child, for the
    // background — at the root.
    const auto makeCreateMenu = [this, entityId] {
        auto create = std::make_shared<Menu>();
        create->addItem("Empty Entity", [this, entityId] { m_output->hierarchyDidRequestAddChild(entityId); });
        create->addSeparator();
        const std::pair<const char *, PrimitiveKind> primitives[] = {
            {"Cube", PrimitiveKind::Cube},           {"Sphere", PrimitiveKind::Sphere},
            {"Capsule", PrimitiveKind::Capsule},     {"Plane", PrimitiveKind::Plane},
            {"Cylinder", PrimitiveKind::Cylinder},
        };
        for (const auto &[title, kind] : primitives) {
            create->addItem(title, [this, entityId, kind = kind] {
                m_output->hierarchyDidRequestAddPrimitive(entityId, kind);
            });
        }
        return create;
    };
    if (entityId != Panda::UUID(0)) {
        menu->addSubmenu("Add Child", makeCreateMenu());
        menu->addItem("Duplicate", [this, entityId] { m_output->hierarchyDidRequestDuplicate({entityId}); });
        menu->addSeparator();
        menu->addItem("Delete", [this, entityId] { m_output->hierarchyDidRequestDelete({entityId}); });
    } else {
        menu->addSubmenu("Create Entity", makeCreateMenu());
    }

    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

bool HierarchyPanelView::pointerDown(PointerEvent &) {
    m_output->hierarchyDidSelectEntities({}); // click on the background → clear the selection
    return true;
}

bool HierarchyPanelView::keyDown(KeyEvent &event) {
    std::vector<Panda::UUID> selected;
    for (TreeItem item : m_tree->selectedItems()) {
        const std::uint32_t uuid = toUuid(item);
        if (uuid != 0) { selected.push_back(Panda::UUID(uuid)); }
    }
    if (selected.empty()) { return false; }

    if (event.key == Key::Delete || event.key == Key::Backspace) {
        m_output->hierarchyDidRequestDelete(selected);
        return true;
    }
    if (event.key == Key::D && (event.isSuperDown || event.isControlDown)) {
        m_output->hierarchyDidRequestDuplicate(selected);
        return true;
    }
    return false;
}

void HierarchyPanelView::setWorld(Panda::World *world) {
    m_world = world;
    reload();
}

void HierarchyPanelView::setRootTitle(std::string title) {
    if (title.empty()) { title = "World"; }
    if (m_rootTitle == title) { return; }
    m_rootTitle = std::move(title);
    reload();
}

void HierarchyPanelView::reload() {
    m_rootChildren.clear();
    if (m_world && !m_world->isEmpty()) {
        m_world->getAllEntitiesWith<Panda::IdComponent>().each([this](Panda::IdComponent &idc) {
            Panda::Entity entity = m_world->getById(idc.id);
            if (entity.isValid() && !entity.getParent()) { m_rootChildren.push_back(static_cast<std::uint32_t>(idc.id)); }
        });
    }
    m_tree->reloadData();
}

void HierarchyPanelView::setStatusText(const std::string &text) {
    m_status->setText(text);
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

void HierarchyPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void HierarchyPanelView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
