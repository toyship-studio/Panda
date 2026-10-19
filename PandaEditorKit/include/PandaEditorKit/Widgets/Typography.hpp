#pragma once

#include <PandaUI/Rendering/Font.hpp>

namespace Editor {

// Typographic tokens of the editor (per design.md §2). The sizes are chosen logically by the design
// roles, but with a +1px correction at small point sizes: the "bare" design sizes look smaller in
// the PandaUI render than in the HTML reference; +1 evens them out with the existing Demo (Body = 16,
// like Demo::Style::BodyFont). The calibration lives ONLY here — if something is a bit small/large,
// a single token is adjusted.
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
    inline constexpr Font Body{16.f};     // tree rows, inspector fields, menu items, tabs, buttons
    inline constexpr Font Caption{14.f};  // status bars, asset names, chips, captions, hints (design 13 +1)
    inline constexpr Font Meta{13.f};     // counters, metadata, versions (small, usually muted)

    // Caps labels (uppercase + tracking are applied by the widget; here — point size + weight)
    inline constexpr Font PanelTitle{12.f, FontWeight::Medium};     // panel title (design 11 +1)
    inline constexpr Font ComponentTitle{13.f, FontWeight::Medium}; // component title (design 12 +1)
    inline constexpr Font SectionCaps{14.f, FontWeight::Medium};    // Statistics/World Settings sections (design 13 +1)

    // Monospaced values (numbers, paths, IDs, axes, FPS, versions, hotkeys)
    inline constexpr Font Value = Font::monospaced(15.f);     // inspector/statistics values, axes (design 14 +1)
    inline constexpr Font ValueSmall = Font::monospaced(14.f); // dense tables: path/ID in Asset Manager, log

    // Tracking of caps titles (px), paired with PanelTitle/ComponentTitle/SectionCaps.
    inline constexpr float PanelTitleTracking = 0.9f;
    inline constexpr float ComponentTitleTracking = 0.85f;
    inline constexpr float SectionCapsTracking = 0.5f;

} // namespace Typo

} // namespace Editor
