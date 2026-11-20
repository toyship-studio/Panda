#pragma once

#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/TextField.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// Full-featured color picker: SV palette (saturation/value) + a draggable hue bar, a HEX field,
// a preview and an eyedropper (if the platform provides a system sampler). Stores the color in HSV,
// returns RGB via getColor()/onColorChanged. The alpha of the color it was given is kept and
// returned as it is; with setAlphaEditable it gets a slider of its own and the HEX field takes
// eight digits.
class ColorPicker : public Control {
public:
    using ColorChangedCallback = std::function<void(ColorPicker &)>;

    ColorPicker();
    ~ColorPicker() override;

    void setColor(Color color);
    Color getColor() const;
    // An Opacity row under the HEX field: for a color whose alpha means something (the base
    // color of a material, a sprite). Off by default — the alpha is then carried, not shown.
    void setAlphaEditable(bool editable);
    void setOnColorChanged(ColorChangedCallback callback);

    // System eyedropper: show the screen sampler and return the picked color (asynchronously;
    // on cancellation the callback is not called). The implementation is installed by the platform
    // layer at startup (macOS — NSColorSampler); without it the eyedropper button is not shown in
    // the picker.
    using SystemColorSampler = std::function<void(std::function<void(Color)>)>;
    static void setSystemColorSampler(SystemColorSampler sampler);

private:
    enum class DragTarget {
        None,
        SaturationValue,
        Hue,
    };

    bool pointerDown(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool keepsTouch() const override {
        return m_dragTarget != DragTarget::None; // the finger picks a color: no scroll view takes it
    }
    bool pointerUp(PointerEvent &event) override;
    void draw(float offsetX, float offsetY) override;
    void themeChanged() override;

    void updateFromPointer(Point windowPosition);
    void handleHexInput();
    void applyColorChanged(bool reformatHex, bool notify);

    std::shared_ptr<Panel> m_reservation;
    std::shared_ptr<TextField> m_hexField;
    std::shared_ptr<Panel> m_swatch;
    std::shared_ptr<Panel> m_alphaRow;
    std::shared_ptr<class Slider> m_alphaSlider;
    std::shared_ptr<class Label> m_alphaLabel;
    ColorChangedCallback m_onColorChanged;
    float m_hue;
    float m_saturation;
    float m_value;
    float m_alpha = 1.f;
    bool m_alphaEditable = false;
    DragTarget m_dragTarget;
};

} // namespace PandaUI
