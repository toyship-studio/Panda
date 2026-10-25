#include "UI/VisibilityStrip.hpp"

#include "PandaUI/Elements/Menu.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float STRIP_HEIGHT = 32.f;        // design §3
    constexpr float BTN_W = 26.f, BTN_H = 24.f; // strip icon button (design §3)
    constexpr float ICON = 15.f;

    std::shared_ptr<Panel> makeGroup() {
        auto group = std::make_shared<Panel>();
        group->setBackgroundColor(Color(0x00000000));
        group->layout().setFlexDirection(FlexDirection::Row);
        group->layout().setAlignItems(Align::Center);
        group->layout().setGap(2.f);
        return group;
    }
} // namespace

VisibilityStripView::VisibilityStripView(VisibilityStripDelegate *delegate)
    : m_delegate(delegate) {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setHeight(Length::points(STRIP_HEIGHT));
    layout().setFlexShrink(0.f);
    setThemeStyle([](View &view) { view.setBackgroundColor(theme().surfaceVariant); }); // bg/chrome #191918

    auto topBorder = std::make_shared<Panel>();
    topBorder->setUserInteractionEnabled(false);
    // separates the strip from the bottom dock
    topBorder->setThemeStyle([](View &view) { view.setBackgroundColor(theme().border); });
    topBorder->layout().setHeight(Length::points(1.f));
    topBorder->layout().setFlexShrink(0.f);
    addSubview(topBorder);

    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Center);
    row->layout().setFlexGrow(1.f);
    row->layout().setPadding(Edge::Left, 8.f);
    row->layout().setPadding(Edge::Right, 8.f);

    m_leftGroup = makeGroup();
    row->addSubview(m_leftGroup);

    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    row->addSubview(spacer);

    // Status indicators (Zed style): RAM/VRAM/CPU/GPU in muted text to the LEFT of the
    // right group's buttons (the whole bundle is pushed to the right by the spacer above); the
    // content is updated by the owner (DockManager::setStatusText).
    m_statusLabel = std::make_shared<PandaUI::Label>("");
    m_statusLabel->setFont(PandaUI::Font(14.f));
    m_statusLabel->setTextRole(PandaUI::TextRole::Muted);
    m_statusLabel->setUserInteractionEnabled(false);
    m_statusLabel->layout().setMargin(PandaUI::Edge::Right, 12.f);
    row->addSubview(m_statusLabel);

    m_rightGroup = makeGroup();
    row->addSubview(m_rightGroup);

    addSubview(row);
}

void VisibilityStripView::setStatusText(const std::string &text) {
    m_statusLabel->setText(text);
}

void VisibilityStripView::setItems(const std::vector<StripItem> &items) {
    m_leftGroup->removeAllSubviews();
    m_rightGroup->removeAllSubviews();
    // On the left — panels of the left dock; on the right — of the bottom one, the center, then the
    // right one (Inspector outermost). A panel is shown regardless of the dock and of whether it is
    // open (a closed one — a muted icon).
    for (const StripItem &it : items) {
        if (it.dock == Dock::Left) { m_leftGroup->addSubview(makeButton(it)); }
    }
    for (const StripItem &it : items) {
        if (it.dock == Dock::Bottom) { m_rightGroup->addSubview(makeButton(it)); }
    }
    for (const StripItem &it : items) {
        if (it.dock == Dock::Center) { m_rightGroup->addSubview(makeButton(it)); }
    }
    for (const StripItem &it : items) {
        if (it.dock == Dock::Right) { m_rightGroup->addSubview(makeButton(it)); }
    }
}

std::shared_ptr<IconButton> VisibilityStripView::makeButton(const StripItem &item) {
    auto btn = std::make_shared<IconButton>(item.icon, ICON, BTN_H);
    btn->setButtonSize(BTN_W, BTN_H);
    if (!item.open) { // a closed panel — a dim icon (re-read with the theme)
        btn->setThemeStyle([](View &view) { static_cast<IconButton &>(view).setIconColor(theme().textDim); });
    }
    btn->setActive(item.active);
    const std::string id = item.id;
    btn->setOnClick([this, id] { m_delegate->stripDidActivatePanel(id); });
    IconButton *btnPtr = btn.get(); // lives in the strip group, no cycle
    btn->setOnContextMenu([this, id, btnPtr] { showDockMenu(id, btnPtr); });
    return btn;
}

void VisibilityStripView::showDockMenu(const std::string &id, View *anchor) {
    auto menu = std::make_shared<Menu>();
    menu->addItem("Dock Left", [this, id] { m_delegate->stripDidRequestDock(id, Dock::Left); });
    menu->addItem("Dock Right", [this, id] { m_delegate->stripDidRequestDock(id, Dock::Right); });
    menu->addItem("Dock Bottom", [this, id] { m_delegate->stripDidRequestDock(id, Dock::Bottom); });
    menu->addItem("Dock Center", [this, id] { m_delegate->stripDidRequestDock(id, Dock::Center); });
    const Rect f = anchor->getWorldFrame();
    auto popup = std::make_shared<Popup>(menu, f); // Popup positions it at the anchor itself (off the window edges)
    menu->setOnItemSelected([p = popup.get()] { p->dismiss(); });
    if (Context *ctx = getContext()) { ctx->addOverlay(popup); }
}

} // namespace Editor
