#include "PandaEditorKit/Widgets/EntityWell.hpp"

#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/Icons.hpp"
#include "PandaEditorKit/Widgets/ObjectIcons.hpp"
#include "PandaEditorKit/Widgets/Picker.hpp"
#include "PandaEditorKit/Widgets/Typography.hpp"

#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float WELL_HEIGHT = 28.f; // = the height of text fields
    constexpr float ICON_SIZE = 16.f;

    // The entities of a world in the order of the Hierarchy, depth first: a picker row per
    // entity, its ancestors as the detail.
    void collectEntities(
        Panda::World &world, Panda::Entity entity, const std::string &path, std::vector<PickerItem> &items,
        std::vector<Panda::UUID> &ids
    ) {
        const std::string name = entity.getName();
        items.push_back({name, path, entityIconName(entity)});
        ids.push_back(entity.getId());
        const std::string below = path.empty() ? name : path + " / " + name;
        // A copy: the list belongs to the component.
        const std::vector<Panda::UUID> children = entity.getChildEntities();
        for (Panda::UUID childId : children) {
            Panda::Entity child = world.getById(childId);
            if (child.isValid()) { collectEntities(world, child, below, items, ids); }
        }
    }

} // namespace

// The "icon + name" zone: a click reveals the entity, a double click selects it (an empty field
// opens the picker). A separate Control so that the buttons are not part of the clickable area
// of the value.
class EntityWell::MainZone final : public Control {
public:
    explicit MainZone(EntityWell *owner)
        : m_owner(owner) {
        setCursor(Cursor::PointingHand);
        layout().setFlexGrow(1.f);
        layout().setFlexShrink(1.f);
        layout().setHeight(Length::points(WELL_HEIGHT));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 7.f);
        layout().setGap(6.f);
        surface().setCornerRadius(Radius::sm);
        surface().setBorderWidth(1.f);
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        m_doubleClick = event.button == PointerButton::Left && event.clickCount >= 2;
        return Control::pointerDown(event);
    }
    // The actions run on the release, like every click: selecting rebuilds the form this field
    // sits in, and the release is dispatched with its view kept alive.
    void performPrimaryAction() override {
        const Panda::UUID entity = m_owner->m_entityId;
        const bool doubleClick = m_doubleClick;
        m_doubleClick = false; // the next activation (the keyboard) is a plain click again
        if (!entity) {
            m_owner->openPicker();
        } else if (doubleClick) {
            editorHost().selectEntity(entity);
        } else {
            editorHost().revealEntity(entity);
        }
    }

private:
    EntityWell *m_owner;
    bool m_doubleClick = false; // the press that started this click was the second of a double click
};

EntityWell::EntityWell(Panda::World &world, Panda::UUID initial, std::function<void(Panda::UUID)> onChange)
    : m_world(&world)
    , m_entityId(initial)
    , m_onChange(std::move(onChange)) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexGrow(1.f);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setGap(3.f);

    m_main = std::make_shared<MainZone>(this);
    m_icon = std::make_shared<ImageView>();
    m_icon->setContentMode(ImageContentMode::Fit);
    m_icon->setUserInteractionEnabled(false);
    m_icon->layout().setSize({ICON_SIZE, ICON_SIZE});
    m_icon->layout().setFlexShrink(0.f);
    m_main->addSubview(m_icon);
    m_label = std::make_shared<Label>("");
    m_label->setFont(Typo::Item);
    m_label->setUserInteractionEnabled(false);
    m_label->layout().setFlexShrink(1.f);
    m_main->addSubview(m_label);
    addSubview(m_main);

    auto choose = std::make_shared<IconButton>(Ic::More, Ic::RowSize, WELL_HEIGHT);
    choose->setCornerRadius(Radius::sm);
    choose->setTooltip("Choose Entity");
    choose->setOnClick([this] { openPicker(); });
    addSubview(choose);

    auto clear = std::make_shared<IconButton>(Ic::Clear, Ic::RowSize, WELL_HEIGHT);
    clear->setCornerRadius(Radius::sm);
    clear->setTooltip("Clear Entity");
    clear->setOnClick([this] { setValue(0); });
    m_clearButton = clear;
    addSubview(clear);

    applyStyle();
    refresh();
}

bool EntityWell::canAcceptDrop(const DragInfo &drag) {
    return drag.payload.type == "entity" && !drag.payload.items.empty();
}

void EntityWell::dragEntered(const DragInfo &) {
    m_dropHovered = true;
    applyStyle();
}

void EntityWell::dragExited() {
    m_dropHovered = false;
    applyStyle();
}

bool EntityWell::performDrop(const DragInfo &drag) {
    m_dropHovered = false;
    applyStyle();
    setValue(Panda::UUID(drag.payload.items.front()));
    return true;
}

void EntityWell::setValue(Panda::UUID id) {
    m_entityId = id;
    refresh();
    if (m_onChange) { m_onChange(m_entityId); }
}

void EntityWell::openPicker() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    std::vector<PickerItem> items;
    std::vector<Panda::UUID> ids;
    std::vector<Panda::Entity> roots;
    m_world->getAllEntitiesWith<Panda::IdComponent>().each([&](Panda::IdComponent &id) {
        Panda::Entity entity = m_world->getById(id.id);
        if (entity.isValid() && !entity.getParent()) { roots.push_back(entity); }
    });
    std::sort(roots.begin(), roots.end(), [](Panda::Entity &a, Panda::Entity &b) { return a.getName() < b.getName(); });
    for (Panda::Entity root : roots) { collectEntities(*m_world, root, {}, items, ids); }
    const Rect frame = m_main->getWorldFrame();
    // The form may be rebuilt while the picker is open (an undo): the choice then finds no field.
    std::weak_ptr<View> alive = weak_from_this();
    showPicker(
        context, Point(frame.origin.x, frame.origin.y + frame.size.height), "Search entities…", std::move(items),
        [this, alive, ids](int index) {
            if (!alive.expired()) { setValue(ids[static_cast<size_t>(index)]); }
        }
    );
}

void EntityWell::themeChanged() {
    applyStyle();
    refresh(); // the tint of the icon depends on the theme
}

void EntityWell::refresh() {
    Panda::Entity entity = m_entityId ? m_world->getById(m_entityId) : Panda::Entity();
    const bool hasValue = static_cast<bool>(m_entityId);
    if (!hasValue) {
        m_label->setText("None (entity)");
    } else {
        m_label->setText(entity.isValid() ? entity.getName() : "Missing entity");
    }
    m_label->setTextRole(hasValue && entity.isValid() ? TextRole::Primary : TextRole::Muted);
    m_icon->setTexture(icons().get(entityIconName(entity)));
    m_icon->setTintColor(hasValue && entity.isValid() ? Theme::current().textPrimary : Theme::current().textMuted);
    // The clear button exists only while there is something to clear.
    m_clearButton->setHidden(!hasValue);
    m_clearButton->layout().setDisplay(hasValue ? Display::Flex : Display::None);
}

void EntityWell::applyStyle() {
    const Theme &t = Theme::current();
    m_main->setBackgroundColor(m_dropHovered ? t.controlHover : t.control);
    m_main->surface().setBorderColor(m_dropHovered ? t.accent : t.border);
}

} // namespace Editor
