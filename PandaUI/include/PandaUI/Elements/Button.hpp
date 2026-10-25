#pragma once

#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

// Semantic button style. Colors are taken from the current theme by style + state.
enum class ButtonStyle {
    Neutral,       // neutral (secondary) button
    Accent,        // accent (primary action) — solid fill
    AccentOutline, // outline-primary: soft accent backing + accent text (Add Component / New)
    Link,          // text link button: no background or border, accent text (hover — lighter)
    Destructive,   // dangerous primary action (deletion): solid fill with theme().danger
};

// Button size: font size + padding as a single value. The button height is determined by its
// content — there is no need to fix it from outside (layout().setHeight): padding that does not fit
// into the given height squashes the title box.
enum class ButtonSize {
    Regular, // dialogs, forms, main flow: font size 16, height 28
    Small,   // inspector rows and dense panels: font size 14, height 22
};

class Button : public Control {
public:
    using ClickCallback = std::function<void(Button &)>;

    Button();
    explicit Button(std::string text);

    void setText(std::string text);
    const std::string &getText() const {
        return m_titleLabel->getText();
    }

    void setStyle(ButtonStyle style);
    void setSize(ButtonSize size);
    void setOnClick(ClickCallback callback);
    void setFont(Font font); // family + style + size in a single setter (UIKit-style)
    void setNumberOfLines(int numberOfLines);
    std::shared_ptr<Label> getTitleLabel() const {
        return m_titleLabel;
    }

    bool isPressed() const {
        return isHighlighted();
    }

private:
    void performPrimaryAction() override;
    void controlStateChanged(ControlState previousState, ControlState currentState) override;
    void themeChanged() override;
    void applyThemeColors();
    void applySize();
    void updateAppearance();

    std::shared_ptr<Label> m_titleLabel;
    ClickCallback m_onClick;
    ButtonStyle m_style;
    ButtonSize m_size;
    Color m_normalColor;
    Color m_hoveredColor;
    Color m_pressedColor;
    Color m_disabledColor;
};

} // namespace PandaUI
