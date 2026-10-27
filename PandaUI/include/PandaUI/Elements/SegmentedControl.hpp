#pragma once

#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

// Horizontal exclusive selector: several segments, the selected one is always visible.
// Unlike Combobox (a dropdown list for "many" options), there are few options here
// and all are in plain view — tool modes, Local/World, etc. Single-selection.
// Built as a track container with inner segment Controls (like ListView rows).
// The look of a segmented control: the track, the plates of the segments and their text per state,
// the font, the geometry.
struct SegmentedControlAppearance {
    ThemeColor track = ThemeColor::control;
    ThemeColor trackFocusBorder = ThemeColor::borderStrong; // a soft border around the whole control in focus
    SurfaceImage trackImage;
    ThemeColor segmentSelected = ThemeColor::accentSecondary; // the selected plate; does not depend on the focus
    ThemeColor segmentHovered = ThemeColor::controlHover;
    ThemeColor segmentSelectedDisabled = ThemeColor::controlPressed;
    SurfaceImage segmentSelectedImage;
    ThemeColor textSelected = ThemeColor::onAccent;
    ThemeColor textNormal = ThemeColor::textMuted;
    ThemeColor textHovered = ThemeColor::textPrimary;
    ThemeColor textDisabled = ThemeColor::textDisabled;
    Font font = Font(16.f);
    float height = 28.f; // the height of the fields and buttons
    float trackRadius = Radius::sm;
    float segmentRadius = Radius::xs; // nested: the track radius less its padding
    float trackPadding = 2.f;
    float segmentPaddingX = 10.f; // breathing room around the label
    float minSegmentWidth = 36.f;
    float focusBorderWidth = 1.f;
};

class SegmentedControl : public Panel {
public:
    using SelectionChangedCallback = std::function<void(SegmentedControl &)>;

    SegmentedControl();
    ~SegmentedControl() override;

    // The look of the controls created from now on (Appearance.hpp).
    static SegmentedControlAppearance &defaultAppearance();
    void setAppearance(const SegmentedControlAppearance &appearance);
    const SegmentedControlAppearance &appearance() const {
        return m_appearance;
    }

    void setSegments(std::vector<std::string> segments);
    void setSelectedIndex(int index); // -1 — no selection
    int selectedIndex() const {
        return m_selectedIndex;
    }
    std::string selectedText() const;

    void setOnSelectionChanged(SelectionChangedCallback callback);

    bool keyDown(KeyEvent &event) override; // ←/↑ — previous segment, →/↓ — next (when focused)

private:
    class Segment;

    void rebuild();
    void selectFromUser(int index);
    void applyAppearance(); // the geometry of the track; the segments are rebuilt
    void applyStyles();
    void updateFocusBorder(); // soft neutral border around the whole control when focused (not flashy)
    void enabledChanged(bool enabled) override;
    void focusChanged(bool focused) override;
    void themeChanged() override;

    SegmentedControlAppearance m_appearance;
    std::vector<std::string> m_segments;
    std::vector<Segment *> m_segmentViews; // ownership is in m_subviews; non-owning here
    int m_selectedIndex = -1;
    SelectionChangedCallback m_onSelectionChanged;
};

} // namespace PandaUI
