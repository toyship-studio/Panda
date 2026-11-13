#include "PandaEditorKit/Widgets/EntityWell.hpp"
#include <PandaEditorKit/Widgets/Typography.hpp>

#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/Icons.hpp"

#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>

#include <utility>

namespace Editor {

using namespace PandaUI;

EntityWell::EntityWell(Panda::World &world, Panda::UUID initial, std::function<void(Panda::UUID)> onChange)
    : m_world(&world)
    , m_entityId(initial)
    , m_onChange(std::move(onChange)) {
    setCursor(Cursor::PointingHand);
    layout().setFlexGrow(1.f);
    layout().setHeight(Length::points(28.f));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setPadding(Edge::Left, 8.f);
    layout().setPadding(Edge::Right, 2.f);
    surface().setCornerRadius(Radius::sm);
    surface().setBorderWidth(1.f);
    m_label = std::make_shared<Label>("");
    m_label->setFont(Typo::Item);
    m_label->setUserInteractionEnabled(false);
    m_label->layout().setFlexGrow(1.f);
    m_label->layout().setFlexShrink(1.f);
    addSubview(m_label);
    auto clear = std::make_shared<IconButton>(Ic::Clear, Ic::RowSize, 24.f);
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

void EntityWell::themeChanged() {
    applyStyle();
}

bool EntityWell::pointerDown(PointerEvent &event) {
    m_doubleClick = event.button == PointerButton::Left && event.clickCount >= 2;
    return Control::pointerDown(event);
}

// Both actions run on the release, like every click: selecting rebuilds the form this field sits
// in, and the release is dispatched with its view kept alive.
void EntityWell::performPrimaryAction() {
    const Panda::UUID entity = m_entityId;
    const bool doubleClick = m_doubleClick;
    m_doubleClick = false; // the next activation (the keyboard) is a plain click again
    if (!entity) { return; } // an empty slot only takes a drop
    if (doubleClick) {
        editorHost().selectEntity(entity);
    } else {
        editorHost().revealEntity(entity);
    }
}

void EntityWell::refresh() {
    std::string name;
    if (m_entityId) {
        Panda::Entity entity = m_world->getById(m_entityId);
        name = entity.isValid() ? entity.getName() : "Missing entity";
    }
    m_label->setText(name.empty() ? "None (entity)" : name);
    m_label->setTextRole(name.empty() ? TextRole::Muted : TextRole::Primary);
    // The clear button exists only while there is something to clear.
    const bool hasValue = static_cast<bool>(m_entityId);
    m_clearButton->setHidden(!hasValue);
    m_clearButton->layout().setDisplay(hasValue ? Display::Flex : Display::None);
}

void EntityWell::applyStyle() {
    const Theme &t = Theme::current();
    setBackgroundColor(m_dropHovered ? t.controlHover : t.control);
    surface().setBorderColor(m_dropHovered ? t.accent : t.border);
}

} // namespace Editor
