#include "UI/Widgets/PanelHeader.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cctype>
#include <string>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float HEADER_HEIGHT = 30.f; // title row; +1px divider = 31 total (design §3)

    std::string toUpperAscii(std::string s) {
        for (char &c : s) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        return s;
    }
} // namespace

PanelHeader::PanelHeader(std::string title)
    : m_title(toUpperAscii(std::move(title))) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void PanelHeader::addSubviews() {
    // Row: title + counter (left), spacer, accessory slot (right).
    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Center);
    row->layout().setHeight(Length::points(HEADER_HEIGHT));
    row->layout().setGap(6.f);
    row->layout().setPadding(Edge::Left, 12.f);
    row->layout().setPadding(Edge::Right, 6.f);

    // The panel title is a navigation label: uppercase 11px, tracking, muted (design §2).
    m_label = std::make_shared<Label>(m_title);
    m_label->setFont(Typo::PanelTitle);
    m_label->setLetterSpacing(Typo::PanelTitleTracking);
    m_label->setTextRole(TextRole::Muted);
    row->addSubview(m_label);

    // Counter (e.g. the number of entities) — small, very muted; hidden until set.
    m_countLabel = std::make_shared<Label>("");
    m_countLabel->setFont(Typo::Meta);
    m_countLabel->setTextRole(TextRole::Disabled);
    m_countLabel->setHidden(true);
    m_countLabel->layout().setDisplay(Display::None);
    row->addSubview(m_countLabel);

    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    row->addSubview(spacer);

    m_accessorySlot = std::make_shared<Panel>();
    m_accessorySlot->setBackgroundColor(Color(0x00000000));
    m_accessorySlot->layout().setFlexDirection(FlexDirection::Row);
    m_accessorySlot->layout().setAlignItems(Align::Center);
    m_accessorySlot->layout().setGap(2.f);
    row->addSubview(m_accessorySlot);

    // Maximize button (fullscreen toggle of the zone) — rightmost; visible only when a handler is set.
    m_maximizeBtn = std::make_shared<IconButton>(Ic::Maximize, Ic::HeaderSize, 23.f);
    m_maximizeBtn->setTooltip("Toggle Fullscreen");
    m_maximizeBtn->layout().setDisplay(Display::None);
    row->addSubview(m_maximizeBtn);

    addSubview(row);

    // A thin bottom divider (border/soft) separates the header from the panel body.
    m_divider = std::make_shared<Panel>();
    m_divider->setUserInteractionEnabled(false);
    m_divider->layout().setHeight(Length::points(1.f));
    addSubview(m_divider);
}

void PanelHeader::setCount(int count) {
    const bool show = count >= 0;
    m_countLabel->setText(show ? std::to_string(count) : std::string());
    m_countLabel->setHidden(!show);
    m_countLabel->layout().setDisplay(show ? Display::Flex : Display::None);
}

void PanelHeader::setAccessory(std::shared_ptr<View> accessory) {
    m_accessorySlot->removeAllSubviews();
    if (accessory) { m_accessorySlot->addSubview(std::move(accessory)); }
}

void PanelHeader::setOnMaximize(std::function<void()> callback) {
    m_maximizeBtn->setOnClick(std::move(callback));
    m_maximizeBtn->layout().setDisplay(Display::Flex);
}

void PanelHeader::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setFlexShrink(0.f); // the header has a fixed height, does not shrink
}

void PanelHeader::setupStyle() {
    // Transparent: the header lives on the panel background (windowBackground), separated by a thin
    // line at the bottom.
    setBackgroundColor(Color(0x00000000));
    if (m_divider) { m_divider->setBackgroundColor(ThemeColor::borderSoft); }
}

} // namespace Editor
