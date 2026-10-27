#include "PandaUI/Elements/SegmentedControl.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

// A single segment: a Control with a label; on click it reports its index to the owner. The selected
// one is a "raised" plate (surface) on the dark track, the others are transparent.
class SegmentedControl::Segment final : public Control {
public:
    Segment(SegmentedControl *owner, int index, std::string text)
        : m_owner(owner)
        , m_index(index)
        , m_label(std::make_shared<Label>(std::move(text))) {
        const SegmentedControlAppearance &a = owner->appearance();
        setCursor(Cursor::PointingHand);
        setFocusable(false); // the control itself holds the focus (a single tab stop, arrow keys go to it)
        layout().setFlexGrow(1.f); // segments share the width equally
        layout().setFlexShrink(1.f);
        layout().setMinWidth(Length::points(a.minSegmentWidth));
        layout().setPadding(Edge::Horizontal, a.segmentPaddingX);
        layout().setJustifyContent(Justify::Center);
        layout().setAlignItems(Align::Center);
        surface().setCornerRadius(a.segmentRadius);

        m_label->setFont(a.font);
        m_label->setTextAlignment(TextAlignment::Center);
        addSubview(m_label);

        applyStyle();
    }

    void setSegmentSelected(bool selected) {
        if (m_selected == selected) { return; }
        m_selected = selected;
        applyStyle();
    }

protected:
    void performPrimaryAction() override {
        m_owner->selectFromUser(m_index);
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
    }

private:
    void applyStyle() {
        const SegmentedControlAppearance &a = m_owner->appearance();
        const Color transparent(0.f, 0.f, 0.f, 0.f);
        if (hasControlState(ControlState::Disabled)) {
            setBackgroundColor(m_selected ? a.segmentSelectedDisabled : ThemeColor(transparent));
            m_label->setTextColor(a.textDisabled);
        } else if (m_selected) {
            // The selected one — a permanent plate (the color does NOT depend on the focus).
            setBackgroundColor(a.segmentSelected);
            m_label->setTextColor(a.textSelected);
        } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(a.segmentHovered);
            m_label->setTextColor(a.textHovered);
        } else {
            setBackgroundColor(transparent);
            m_label->setTextColor(a.textNormal);
        }
        if (m_selected && a.segmentSelectedImage) {
            surface().setBackgroundImage(a.segmentSelectedImage);
        } else {
            surface().clearBackgroundImage();
        }
    }

    SegmentedControl *m_owner;
    int m_index;
    std::shared_ptr<Label> m_label;
    bool m_selected = false;
};

SegmentedControlAppearance &SegmentedControl::defaultAppearance() {
    static SegmentedControlAppearance appearance;
    return appearance;
}

SegmentedControl::SegmentedControl()
    : m_appearance(defaultAppearance()) {
    setFocusable(true); // the control accepts focus → arrow keys switch the segment, the focus border is visible
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
    setClipsToBounds(true); // the rounded track clips the segment plates
    applyAppearance();
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::setAppearance(const SegmentedControlAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
    rebuild(); // the segments take their geometry and font at creation
}

void SegmentedControl::applyAppearance() {
    layout().setHeight(Length::points(m_appearance.height));
    layout().setPadding(m_appearance.trackPadding);
    surface().setCornerRadius(m_appearance.trackRadius);
    applyStyles();
    updateFocusBorder();
}

void SegmentedControl::setSegments(std::vector<std::string> segments) {
    m_segments = std::move(segments);
    if (m_selectedIndex >= static_cast<int>(m_segments.size())) {
        m_selectedIndex = m_segments.empty() ? -1 : static_cast<int>(m_segments.size()) - 1;
    } else if (m_selectedIndex < 0 && !m_segments.empty()) {
        m_selectedIndex = 0; // the first one is selected by default
    }
    rebuild();
}

void SegmentedControl::setSelectedIndex(int index) {
    const int clamped = (index >= 0 && index < static_cast<int>(m_segments.size())) ? index : -1;
    if (clamped == m_selectedIndex) { return; }
    m_selectedIndex = clamped;
    applyStyles();
}

std::string SegmentedControl::selectedText() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_segments.size())) {
        return m_segments[m_selectedIndex];
    }
    return {};
}

void SegmentedControl::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}

void SegmentedControl::selectFromUser(int index) {
    if (index == m_selectedIndex || index < 0 || index >= static_cast<int>(m_segments.size())) { return; }
    m_selectedIndex = index;
    applyStyles();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void SegmentedControl::rebuild() {
    removeAllSubviews();
    m_segmentViews.clear();
    for (int i = 0; i < static_cast<int>(m_segments.size()); ++i) {
        auto segment = std::make_shared<Segment>(this, i, m_segments[i]);
        segment->setEnabled(isEnabled());
        m_segmentViews.push_back(segment.get());
        addSubview(segment);
    }
    applyStyles();
}

void SegmentedControl::applyStyles() {
    setBackgroundColor(m_appearance.track); // track
    if (m_appearance.trackImage) {
        surface().setBackgroundImage(m_appearance.trackImage);
    } else {
        surface().clearBackgroundImage();
    }
    for (int i = 0; i < static_cast<int>(m_segmentViews.size()); ++i) {
        m_segmentViews[i]->setSegmentSelected(i == m_selectedIndex);
    }
}

void SegmentedControl::enabledChanged(bool enabled) {
    for (Segment *segment : m_segmentViews) { segment->setEnabled(enabled); }
}

void SegmentedControl::themeChanged() {
    applyStyles(); // track; the segments re-theme themselves (their themeChanged)
    updateFocusBorder();
}

bool SegmentedControl::keyDown(KeyEvent &event) {
    if (m_segments.empty()) { return false; }
    if (event.key == Key::Left || event.key == Key::Up) {
        selectFromUser(std::max(0, m_selectedIndex - 1));
        return true;
    }
    if (event.key == Key::Right || event.key == Key::Down) {
        selectFromUser(std::min(static_cast<int>(m_segments.size()) - 1, m_selectedIndex + 1));
        return true;
    }
    return false;
}

void SegmentedControl::focusChanged(bool) {
    updateFocusBorder();
}

void SegmentedControl::updateFocusBorder() {
    // A border around the WHOLE control, shown only in focus. A neutral muted color (borderStrong)
    // and a thin line — soft, not "loud" like an accent outline. The background does not change.
    surface().setBorderWidth(isFocused() ? m_appearance.focusBorderWidth : 0.f);
    surface().setBorderColor(m_appearance.trackFocusBorder);
    setNeedsDisplay();
}

} // namespace PandaUI
