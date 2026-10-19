#pragma once

#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/TextField.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// Full-featured color picker: SV palette (saturation/value) + a draggable hue bar, a HEX field,
// a preview and an eyedropper (if the platform provides a system sampler). Stores the color in HSV,
// returns RGB via getColor()/onColorChanged.
class ColorPicker : public Control {
public:
    using ColorChangedCallback = std::function<void(ColorPicker &)>;

    ColorPicker();
    ~ColorPicker() override;

    void setColor(Color color);
    Color getColor() const;
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
    bool pointerUp(PointerEvent &event) override;
    void draw(float offsetX, float offsetY) override;
    void themeChanged() override;

    void updateFromPointer(Point windowPosition);
    void handleHexInput();
    void applyColorChanged(bool reformatHex, bool notify);

    std::shared_ptr<Panel> m_reservation;
    std::shared_ptr<TextField> m_hexField;
    std::shared_ptr<Panel> m_swatch;
    ColorChangedCallback m_onColorChanged;
    float m_hue;
    float m_saturation;
    float m_value;
    DragTarget m_dragTarget;
};

} // namespace PandaUI
