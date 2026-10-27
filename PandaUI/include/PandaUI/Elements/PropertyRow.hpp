#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/Panel.hpp"

#include <memory>
#include <string>

namespace PandaUI {

class Label;

// Property row: a fixed-width label (left) + an editor (right). The same label width in
// neighboring rows gives an aligned column — the basis of the inspector and forms.
// fill=true stretches the editor to the column width (input fields), false — keeps its
// own size (Toggle, ColorWell, etc.).
// The look of a form row: the label column, the row height, the gap and the label text.
struct PropertyRowAppearance {
    float labelWidth = 120.f;
    float minHeight = 28.f;
    float gap = 12.f;
    Font labelFont = Font(16.f);
    ThemeColor labelColor = ThemeColor::textMuted; // a form field label — muted
};

class PropertyRow : public Panel {
public:
    PropertyRow(std::string title, std::shared_ptr<View> editor, bool fill = true);
    explicit PropertyRow(std::string title);
    ~PropertyRow() override;

    // The look of the rows created from now on (Appearance.hpp).
    static PropertyRowAppearance &defaultAppearance();
    void setAppearance(const PropertyRowAppearance &appearance);
    const PropertyRowAppearance &appearance() const {
        return m_appearance;
    }

    void setTitle(std::string title);
    void setEditor(std::shared_ptr<View> editor, bool fill = true);
    void setLabelWidth(float width);
    // Lines of the label: 0 by default — as many as the title needs, the row grows with it; 1 cuts
    // a long title with an ellipsis (a single-height row that must not grow).
    void setLabelNumberOfLines(int numberOfLines);
    // Mark that the value differs between the selected objects (multi-selection): a "* " marker
    // is added to the label. The "mixed" state of the editor itself is set on it separately.
    void setMixed(bool mixed);

private:
    void refreshLabel();
    void applyAppearance();

    PropertyRowAppearance m_appearance;
    std::shared_ptr<Label> m_label;
    std::shared_ptr<View> m_editor;
    std::string m_title;
    bool m_mixed = false;
};

} // namespace PandaUI
