#include "UI/Sections/ContextMenuArea.hpp"

#include "UI/Demo/DemoStyle.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

ContextMenuArea::ContextMenuArea() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ContextMenuArea::addSubviews() {
    m_hint = std::make_shared<Label>("Right-click here");
    m_hint->setFont(Font(Style::BodyFont));
    m_hint->setTextRole(TextRole::Secondary);
    addSubview(m_hint);
}

void ContextMenuArea::setupLayout() {
    layout().setFlexGrow(1.f);
    layout().setHeight(Length::points(120.f));
    layout().setFlexDirection(FlexDirection::Column);
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);
}

void ContextMenuArea::setupStyle() {
    setBackgroundColor(theme().surfaceVariant);
    surface().setCornerRadius(Style::CardRadius);
    surface().setBorderWidth(1.f);
    surface().setBorderColor(theme().border);
}

bool ContextMenuArea::pointerDown(PointerEvent &event) {
    if (event.button != PointerButton::Right) { return false; }
    showMenu(event.position);
    return true;
}

void ContextMenuArea::showMenu(Point position) {
    Context *context = getContext();
    if (!context) { return; }

    Label *hint = m_hint.get();
    auto menu = std::make_shared<Menu>();
    menu->addItem("Cut", [hint] { hint->setText("Context: Cut"); });
    menu->addItem("Copy", [hint] { hint->setText("Context: Copy"); });
    menu->addItem("Paste", [hint] { hint->setText("Context: Paste"); });
    menu->addSeparator();

    // Submenu (hover or →/Enter opens it; nesting is unlimited).
    auto insert = std::make_shared<Menu>();
    insert->addItem("Rectangle", [hint] { hint->setText("Insert: Rectangle"); });
    insert->addItem("Circle", [hint] { hint->setText("Insert: Circle"); });
    auto advanced = std::make_shared<Menu>();
    advanced->addItem("Bezier", [hint] { hint->setText("Insert: Bezier"); });
    advanced->addItem("Polygon", [hint] { hint->setText("Insert: Polygon"); });
    insert->addSubmenu("Advanced", advanced);
    menu->addSubmenu("Insert", insert);

    menu->addSeparator();
    menu->addItem("Select All", [hint] { hint->setText("Context: Select All"); });

    // The menu pops up from the cursor point: the anchor is a zero-size rectangle at the click position.
    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

} // namespace Demo
