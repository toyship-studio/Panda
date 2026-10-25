#include "PandaEditorKit/Widgets/EntityWell.hpp"

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
    layout().setJustifyContent(Justify::Center);
    layout().setPadding(Edge::Horizontal, 8.f);
    surface().setCornerRadius(Radius::sm);
    surface().setBorderWidth(1.f);
    m_label = std::make_shared<Label>("");
    m_label->setFont(Font(15.f));
    addSubview(m_label);
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
    m_entityId = Panda::UUID(drag.payload.items.front());
    refresh();
    applyStyle();
    if (m_onChange) { m_onChange(m_entityId); }
    return true;
}

void EntityWell::themeChanged() {
    applyStyle();
}

void EntityWell::performPrimaryAction() {
    if (!m_entityId) { return; }
    m_entityId = 0;
    refresh();
    if (m_onChange) { m_onChange(0); }
}

void EntityWell::refresh() {
    std::string name;
    if (m_entityId) {
        Panda::Entity entity = m_world->getById(m_entityId);
        name = entity.isValid() ? entity.getName() : "Missing entity";
    }
    m_label->setText(name.empty() ? "None (entity)" : name);
    m_label->setTextRole(name.empty() ? TextRole::Muted : TextRole::Primary);
}

void EntityWell::applyStyle() {
    const Theme &t = theme();
    setBackgroundColor(m_dropHovered ? t.controlHover : t.control);
    surface().setBorderColor(m_dropHovered ? t.accent : t.border);
}

} // namespace Editor
