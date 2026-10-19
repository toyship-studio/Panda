#pragma once

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
class SegmentedControl : public Panel {
public:
    using SelectionChangedCallback = std::function<void(SegmentedControl &)>;

    SegmentedControl();
    ~SegmentedControl() override;

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
    void applyStyles();
    void updateFocusBorder(); // soft neutral border around the whole control when focused (not flashy)
    void enabledChanged(bool enabled) override;
    void focusChanged(bool focused) override;
    void themeChanged() override;

    std::vector<std::string> m_segments;
    std::vector<Segment *> m_segmentViews; // ownership is in m_subviews; non-owning here
    int m_selectedIndex = -1;
    SelectionChangedCallback m_onSelectionChanged;
};

} // namespace PandaUI
