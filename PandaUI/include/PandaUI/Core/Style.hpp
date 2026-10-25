#pragma once

#include "PandaUI/Core/Base.hpp"

namespace PandaUI {

class View;

// The geometry scale of PandaUI — the scale of GPUI (Tailwind at 16 px): every radius, padding and
// shadow of the library is one of these values, so the controls read as one family and "sm" means
// the same thing everywhere. Colors belong to the theme; geometry is not themed.

namespace Radius {
    inline constexpr float none = 0.f;
    inline constexpr float xs = 2.f;      // nested inside sm: a selected segment inside its track
    inline constexpr float sm = 4.f;      // buttons, inputs, list rows, menu items, chips, section headers
    inline constexpr float md = 6.f;      // small plates: previews, the transport capsule, a drag ghost
    inline constexpr float lg = 8.f;      // floating surfaces and cards: popovers, menus, tooltips, dialogs
    inline constexpr float full = 9999.f; // pills: toggles, sliders, progress bars
} // namespace Radius

namespace Spacing {
    inline constexpr float xs = 2.f;
    inline constexpr float sm = 4.f;  // inside a control: icon to label, menu padding
    inline constexpr float md = 8.f;  // the padding of a button, a field, a menu item
    inline constexpr float lg = 12.f; // between controls in a row, the padding of a popover card
    inline constexpr float xl = 16.f; // the padding of a dialog or a card
    inline constexpr float xxl = 24.f; // the padding of a page
} // namespace Spacing

struct ShadowStyle {
    Color color;
    Point offset;
    float radius = 0.f;
    float spread = 0.f;
};

// Two shadows, like the elevations of GPUI: a hairline lift under a popover and a soft one under a
// modal. Buttons, fields and cards are flat — a border separates them from the surface below.
namespace Shadow {
    inline const ShadowStyle none{Color(0.f, 0.f, 0.f, 0.f), Point(0.f, 0.f), 0.f};
    inline const ShadowStyle popover{Color(0.f, 0.f, 0.f, 0.16f), Point(0.f, 2.f), 4.f};
    inline const ShadowStyle modal{Color(0.f, 0.f, 0.f, 0.22f), Point(0.f, 4.f), 12.f};
} // namespace Shadow

// The floating surfaces (GPUI's elevation_2 and elevation_3): the surface background, a 1 px border,
// the lg radius and the shadow of the level — one call instead of five per popover.
enum class Elevation {
    Popover, // menus, comboboxes, color pickers, tooltips, floating panels
    Modal,   // dialogs
};
void applyElevation(View &view, Elevation elevation);

} // namespace PandaUI
