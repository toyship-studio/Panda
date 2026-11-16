#pragma once

#include "PandaUI/Elements/Control.hpp"

#include <functional>

namespace PandaUI {

class Popup;

// Interactive color swatch (the "editable" variation): shows the color and, when pressed,
// opens a ColorPicker in a popup overlay. Editing in the palette updates the color live.
class ColorWell : public Control {
public:
    using ColorChangedCallback = std::function<void(ColorWell &)>;

    ColorWell();
    explicit ColorWell(Color color);
    ~ColorWell() override;
    // The picker of the well gets an Opacity slider (ColorPicker::setAlphaEditable).
    void setAlphaEditable(bool editable) {
        m_alphaEditable = editable;
    }

    void setColor(Color color);
    Color getColor() const {
        return m_color;
    }
    void setOnColorChanged(ColorChangedCallback callback);
    // The color as "#RRGGBBAA".
    void describeAccessibility(Accessibility &accessibility) const override;

private:
    void performPrimaryAction() override;
    void controlStateChanged(ControlState previous, ControlState current) override;
    void themeChanged() override;
    bool keyDown(KeyEvent &event) override; // Escape closes the picker; Enter/Space opens it (via Control)
    void apply();
    void openPicker();

    Color m_color;
    bool m_alphaEditable = false;
    ColorChangedCallback m_onColorChanged;
    Popup *m_picker = nullptr; // the open picker (or null) — to avoid opening duplicates in a stack
};

} // namespace PandaUI
