#pragma once

#include <PandaUI/Rendering/Font.hpp>

namespace Editor {

// Typographic roles of the editor. Text takes its font from a role, never from a number: when
// something reads too small or too large, the role is adjusted here and every place follows.
//
// The floor: nothing in the editor is set smaller than 14. A text that seems to deserve a smaller
// size is a text to remove, not to shrink. The body is 16 and the editor scales every role with
// the UI font size of the settings.
//
// letterSpacing/uppercase are NOT part of Font (paragraph attributes). For caps titles the tracking
// constants are kept alongside; the uppercase text and setLetterSpacing are applied by the widget
// itself (PanelHeader etc.).
namespace Typo {

    using PandaUI::Font;
    using PandaUI::FontWeight;

    // Titles
    inline constexpr Font WindowTitle{24.f, FontWeight::Bold};    // Start Screen / About — large title
    inline constexpr Font DialogTitle{18.f, FontWeight::Bold};    // title of a dialog / an example block

    // Body text
    inline constexpr Font Body{16.f};    // tree rows, inspector fields, menu items, tabs, buttons
    inline constexpr Font Item{15.f};    // names of things: an asset in the grid, the value of a well, a chip
    inline constexpr Font Caption{14.f}; // status bars, hints, counters, banners (the smallest text)

    // Caps labels (uppercase + tracking are applied by the widget; here — point size + weight)
    inline constexpr Font PanelTitle{14.f, FontWeight::Medium};     // panel title
    inline constexpr Font ComponentTitle{14.f, FontWeight::Medium}; // component and settings section title
    inline constexpr Font SectionCaps{15.f, FontWeight::Medium};    // Statistics sections

    // Monospaced values (numbers, paths, IDs, axes, FPS, versions, hotkeys)
    inline constexpr Font Value = Font::monospaced(15.f);      // inspector/statistics values, axes
    inline constexpr Font ValueSmall = Font::monospaced(14.f); // dense rows: the log, a path

    // Tracking of caps titles (px), paired with PanelTitle/ComponentTitle/SectionCaps.
    inline constexpr float PanelTitleTracking = 0.9f;
    inline constexpr float ComponentTitleTracking = 0.85f;
    inline constexpr float SectionCapsTracking = 0.5f;

} // namespace Typo

} // namespace Editor
