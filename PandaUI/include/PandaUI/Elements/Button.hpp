#pragma once

#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

// Semantic button style: a preset of the appearance (Button::defaultAppearance(style)).
enum class ButtonStyle {
    Neutral,       // neutral (secondary) button
    Accent,        // accent (primary action) — solid fill
    AccentOutline, // outline-primary: soft accent backing + accent text (Add Component / New)
    Link,          // text link button: no background or border, accent text (hover — lighter)
    Destructive,   // dangerous primary action (deletion): solid fill with the danger color
};

// Button size: font size + padding as a single value (Button::defaultMetrics(size)). The button
// height is determined by its content — there is no need to fix it from outside (layout().setHeight):
// padding that does not fit into the given height squashes the title box.
enum class ButtonSize {
    Regular, // dialogs, forms, main flow: font size 16, height 28
    Small,   // inspector rows and dense panels: font size 14, height 22
};

// The look of a button: the plate and the title color in each state, the font, the geometry. The
// presets of the editor are the defaults; a game replaces the plates with nine-slice images, the
// font with its own, the radius with its shape.
struct ButtonAppearance {
    struct State {
        SurfaceStyle surface;
        ThemeColor text = ThemeColor::textPrimary;
    };
    State normal, hovered, pressed, focused, disabled; // focused: the keyboard focus without the cursor

    Font font = Font(16.f);
    EdgeInsets padding = EdgeInsets(5.f, Spacing::lg, Spacing::lg, 5.f);
    float minHeight = 28.f;
    CornerRadii cornerRadius = CornerRadii(Radius::sm);
    float borderWidth = 1.f;
    TextShadow titleShadow;
    TextOutline titleOutline;
};

// The metrics of a ButtonSize: applied over the appearance of the style.
struct ButtonMetrics {
    float fontSize = 16.f;
    EdgeInsets padding = EdgeInsets(5.f, Spacing::lg, Spacing::lg, 5.f);
    float minHeight = 28.f;
};

class Button : public Control {
public:
    using ClickCallback = std::function<void(Button &)>;

    Button();
    explicit Button(std::string text);

    // The look of the buttons of a style created from now on, and the metrics of a size
    // (Appearance.hpp). setStyle takes the appearance of the style, setSize lays the metrics over it.
    static ButtonAppearance &defaultAppearance(ButtonStyle style);
    static ButtonMetrics &defaultMetrics(ButtonSize size);
    void setAppearance(const ButtonAppearance &appearance); // this button only, as given
    const ButtonAppearance &appearance() const {
        return m_appearance;
    }

    void setText(std::string text);
    const std::string &getText() const {
        return m_titleLabel->getText();
    }

    void setStyle(ButtonStyle style);
    ButtonStyle getStyle() const {
        return m_style;
    }
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
    const ButtonAppearance::State &currentState() const;
    void applyMetrics();    // the size over the appearance
    void applyAppearance(); // geometry, font, decorations; then the state colors
    void updateAppearance(); // the plate and the title color of the current state

    std::shared_ptr<Label> m_titleLabel;
    ClickCallback m_onClick;
    ButtonStyle m_style;
    ButtonSize m_size;
    ButtonAppearance m_appearance;
};

} // namespace PandaUI
