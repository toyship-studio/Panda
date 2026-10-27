#pragma once

// The appearance of the widgets — what UIAppearance is to UIKit. Every widget carries a struct of
// its look: the colors (theme tokens by default, so a theme change reaches an appearance nobody
// touched), the images, the fonts and the geometry that used to be compile-time constants.
// `Widget::defaultAppearance()` is the look of every instance created from now on, the editor's by
// default; a game edits it once at start and every control follows. `setAppearance` restyles one
// instance. Nothing is themed by the geometry: a theme is colors, an appearance is the shape too.

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Rendering/Font.hpp" // the Font descriptor of the appearances
#include "PandaUI/Rendering/Surface.hpp"

#include <optional>
#include <utility>

namespace PandaUI {

// The surface of a control in one of its states: a fill, a gradient or an image over it, a border
// color. The geometry — the radius, the border width — belongs to the appearance of the widget.
struct SurfaceStyle {
    ThemeColor background = Color(0.f, 0.f, 0.f, 0.f);
    ThemeColor border = Color(0.f, 0.f, 0.f, 0.f);
    std::optional<SurfaceGradient> gradient; // instead of the background color while set
    SurfaceImage image;                      // over the fill when set: a nine-slice skin

    SurfaceStyle() = default;
    SurfaceStyle(ThemeColor background, ThemeColor border = Color(0.f, 0.f, 0.f, 0.f))
        : background(background)
        , border(border) {}

    SurfaceStyle withImage(SurfaceImage value) const {
        SurfaceStyle copy = *this;
        copy.image = std::move(value);
        return copy;
    }
    SurfaceStyle withGradient(SurfaceGradient value) const {
        SurfaceStyle copy = *this;
        copy.gradient = value;
        return copy;
    }

    void applyTo(Surface &surface) const {
        surface.setBackgroundColor(background);
        if (gradient) {
            surface.setBackgroundGradient(*gradient);
        } else {
            surface.clearBackgroundGradient();
        }
        if (image) {
            surface.setBackgroundImage(image);
        } else {
            surface.clearBackgroundImage();
        }
        surface.setBorderColor(border);
    }
};

} // namespace PandaUI
