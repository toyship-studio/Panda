#pragma once

#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// The look of a slider: the track, the filled part, the thumb and the focus halo, their images,
// the geometry.
struct SliderAppearance {
    ThemeColor track = ThemeColor::input;
    ThemeColor fill = ThemeColor::accent;
    ThemeColor fillDisabled = ThemeColor::control;
    ThemeColor thumb = ThemeColor::knob;
    ThemeColor thumbDisabled = ThemeColor::textDisabled;
    ThemeColor thumbBorder = ThemeColor::borderMed;
    ThemeColor thumbBorderHovered = ThemeColor::borderStrong;
    ThemeColor focusHalo = ThemeColor::focusRing.withAlpha(0.30f); // the soft halo behind the thumb in focus
    SurfaceImage trackImage;
    SurfaceImage fillImage;
    SurfaceImage thumbImage;
    ShadowStyle thumbShadow{Color(0.f, 0.f, 0.f, 0.2f), Point(0.f, 1.f), 2.f};
    float height = 22.f;
    float trackHeight = 4.f;
    float thumbSize = 16.f;
    float thumbBorderWidth = 1.f;
    float defaultWidth = 180.f;
    float focusHaloSize = 8.f; // how much larger the halo is than the thumb
};

class Slider : public Control {
public:
    using ValueChangedCallback = std::function<void(Slider &)>;

    Slider();

    // The look of the sliders created from now on (Appearance.hpp).
    static SliderAppearance &defaultAppearance();
    void setAppearance(const SliderAppearance &appearance);
    const SliderAppearance &appearance() const {
        return m_appearance;
    }

    void setRange(float minimum, float maximum);
    void setValue(float value);
    float getValue() const {
        return m_value;
    }
    float getMinimum() const {
        return m_minimum;
    }
    float getMaximum() const {
        return m_maximum;
    }
    void setOnValueChanged(ValueChangedCallback callback);

private:
    bool pointerDown(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    bool keyDown(KeyEvent &event) override;
    void controlStateChanged(ControlState previous, ControlState current) override;
    void focusChanged(bool focused) override;
    void themeChanged() override;
    void viewDidCalculateLayout() override;

    void applyAppearance();
    void updateAppearance();
    void updateThumbAndFill();
    void setValueFromPointer(const PointerEvent &event);
    void setValueInternal(float value, bool notify);
    float clampValue(float value) const;

    std::shared_ptr<Panel> m_track;
    std::shared_ptr<Panel> m_fill;
    std::shared_ptr<Panel> m_thumb;
    std::shared_ptr<Panel> m_focusRing; // soft halo behind the thumb, visible when focused
    ValueChangedCallback m_onValueChanged;
    SliderAppearance m_appearance;
    float m_minimum;
    float m_maximum;
    float m_value;
    bool m_dragging;
    float m_appliedThumbLeft;
};

} // namespace PandaUI
