#include "PandaUI/Core/Theme.hpp"

#include <utility>

namespace PandaUI {

namespace {

    Theme s_theme = Theme::dark();
    std::function<void()> s_handler;
    uint64_t s_generation = 1;

} // namespace

const Theme &Theme::current() {
    return s_theme;
}

void setTheme(const Theme &theme) {
    s_theme = theme;
    notifyAppearanceChanged();
}

void notifyAppearanceChanged() {
    ++s_generation;
    if (s_handler) { s_handler(); }
}

uint64_t themeGeneration() {
    return s_generation;
}

// The tokens: pointers to the fields, constant-initialized (the constructors are constexpr), so
// they are usable from any static initializer.
const ThemeColor ThemeColor::windowBackground{&Theme::windowBackground};
const ThemeColor ThemeColor::surface{&Theme::surface};
const ThemeColor ThemeColor::surfaceVariant{&Theme::surfaceVariant};
const ThemeColor ThemeColor::input{&Theme::input};
const ThemeColor ThemeColor::inputDeep{&Theme::inputDeep};
const ThemeColor ThemeColor::textPrimary{&Theme::textPrimary};
const ThemeColor ThemeColor::textSecondary{&Theme::textSecondary};
const ThemeColor ThemeColor::textMuted{&Theme::textMuted};
const ThemeColor ThemeColor::textDim{&Theme::textDim};
const ThemeColor ThemeColor::textDisabled{&Theme::textDisabled};
const ThemeColor ThemeColor::accent{&Theme::accent};
const ThemeColor ThemeColor::accentHover{&Theme::accentHover};
const ThemeColor ThemeColor::accentPressed{&Theme::accentPressed};
const ThemeColor ThemeColor::onAccent{&Theme::onAccent};
const ThemeColor ThemeColor::accentSecondary{&Theme::accentSecondary};
const ThemeColor ThemeColor::accentText{&Theme::accentText};
const ThemeColor ThemeColor::accentSoft{&Theme::accentSoft};
const ThemeColor ThemeColor::accentMutedSurface{&Theme::accentMutedSurface};
const ThemeColor ThemeColor::accentMutedBorder{&Theme::accentMutedBorder};
const ThemeColor ThemeColor::control{&Theme::control};
const ThemeColor ThemeColor::controlHover{&Theme::controlHover};
const ThemeColor ThemeColor::controlPressed{&Theme::controlPressed};
const ThemeColor ThemeColor::border{&Theme::border};
const ThemeColor ThemeColor::borderMed{&Theme::borderMed};
const ThemeColor ThemeColor::borderSoft{&Theme::borderSoft};
const ThemeColor ThemeColor::borderFaint{&Theme::borderFaint};
const ThemeColor ThemeColor::borderStrong{&Theme::borderStrong};
const ThemeColor ThemeColor::focusRing{&Theme::focusRing};
const ThemeColor ThemeColor::knob{&Theme::knob};
const ThemeColor ThemeColor::textSelection{&Theme::textSelection};
const ThemeColor ThemeColor::selection{&Theme::selection};
const ThemeColor ThemeColor::selectionInactive{&Theme::selectionInactive};
const ThemeColor ThemeColor::rowHover{&Theme::rowHover};
const ThemeColor ThemeColor::success{&Theme::success};
const ThemeColor ThemeColor::warning{&Theme::warning};
const ThemeColor ThemeColor::danger{&Theme::danger};

void setThemeChangedHandler(std::function<void()> handler) {
    s_handler = std::move(handler);
}

// The colors are taken from the Panda editor theme for Zed (panda-theme, the "Panda" variant, dark).
// Panda palette: blue accent #45A9F9, orange selection #FFB86C, turquoise #19F9D8,
// pink #FF4B82, backgrounds #1E1E1E/#191918.
Theme Theme::dark() {
    Theme t;
    // Surfaces
    t.windowBackground = Color(0x141414FF); // bg/app    — dark main background, panels
    t.surface = Color(0x1E1E1EFF);          // bg/toolbar — toolbar, menus/popups, dialogs
    t.surfaceVariant = Color(0x191918FF);   // bg/chrome  — menu bar, tabs, strips, focused field
    t.input = Color(0x1B1B1BFF);            // bg/input   — input fields, X/Y/Z cells, asset previews
    t.inputDeep = Color(0x161616FF);        // bg/input-2 — search/command line, status bars

    // Text
    t.textPrimary = Color(0xE6E6E6FF);   // text/primary
    t.textSecondary = Color(0xCFCFCFFF); // text/secondary — regular rows
    t.textMuted = Color(0x969696FF);     // text/muted — field labels, glyphs at rest
    t.textDim = Color(0x6E6E6EFF);       // text/dim — captions, placeholders, metadata
    t.textDisabled = Color(0x595959FF);  // text/disabled

    // Accent
    t.accent = Color(0x45A9F9FF);        // text.accent / info
    t.accentHover = Color(0x6FC1FFFF);   // bright blue
    t.accentPressed = Color(0x2F8FE0FF); // derived dark shade
    t.onAccent = Color(0x06121FFF);      // dark text on a solid accent button
    t.accentSecondary = Color(0x12B69DFF);    // green-turquoise (Panda)
    t.accentText = Color(0x9ECBFFFF);         // text/icons on an accent backing
    t.accentSoft = Color(0x45A9F929);         // rgba(69,169,249,.16) — background of the active tab/icon
    t.accentMutedSurface = Color(0x1F2A36FF); // background of the outline primary button
    t.accentMutedBorder = Color(0x2C4156FF);  // border of the outline primary button

    // Neutral surfaces
    t.control = Color(0x2B2B2BFF);        // bg/hover — neutral interactive surface
    t.controlHover = Color(0x353637FF);   // bg/hover-2
    t.controlPressed = Color(0x232323FF);

    // Borders
    t.border = Color(0x303030FF);       // border/strong — between large zones
    t.borderMed = Color(0x2A2A2AFF);    // border/med — input fields, cards
    t.borderSoft = Color(0x262626FF);   // border/soft — panel header separators
    t.borderFaint = Color(0x1F1F1FFF);  // border/faint — row separators
    t.borderStrong = Color(0x3A3A3AFF); // stronger field border on hover
    t.focusRing = Color(0x6FC1FFFF);

    // Miscellaneous
    t.knob = Color(0xFFFFFFFF);
    t.textSelection = Color(0xFFB86C66);     // orange text selection (players.selection)
    t.selection = Color(0xFFB86C24);         // element.selected — selected row in focus (orange ~0.14)
    t.selectionInactive = Color(0x6E6E7340); // soft gray — selected row out of focus
    t.rowHover = Color(0xFFFFFF0D);          // barely noticeable row hover (~5%)

    // Statuses
    t.success = Color(0x19F9D8FF); // created (turquoise)
    t.warning = Color(0xFFCC95FF); // warning / modified
    t.danger = Color(0xFF4B82FF);  // error (pink)
    return t;
}

// Colors from the Panda theme for Zed, the "Panda Light" variant.
// Palette: blue accent #0091FF, orange selection #FF8400, turquoise #12B69D,
// pinkish red #FF0062, backgrounds #F1F1F1/#E6E6E6.
Theme Theme::light() {
    Theme t;
    // Surfaces
    t.windowBackground = Color(0xE6E6E6FF); // panel.background
    t.surface = Color(0xF1F1F1FF);          // editor.background (raised)
    t.surfaceVariant = Color(0xFFFFFFFF);   // white lists/fields on top of gray panels
    t.input = Color(0xFFFFFFFF);            // white input field
    t.inputDeep = Color(0xEDEDEDFF);        // slightly recessed search/command line

    // Text
    t.textPrimary = Color(0x222223FF);   // text/primary
    t.textSecondary = Color(0x4D4D4DFF); // text/secondary
    t.textMuted = Color(0x757575FF);     // text/muted
    t.textDim = Color(0x9A9A9AFF);       // text/dim
    t.textDisabled = Color(0xB0B0B0FF);  // text/disabled

    // Accent
    t.accent = Color(0x0091FFFF);        // info (saturated blue for a light background)
    t.accentHover = Color(0x33A6FFFF);
    t.accentPressed = Color(0x0077D6FF);
    t.onAccent = Color(0xFFFFFFFF);           // white text is readable on the light accent
    t.accentSecondary = Color(0x12B69DFF);    // green-turquoise (Panda created/string)
    t.accentText = Color(0x0A6FC2FF);         // dark blue text on a light accent backing
    t.accentSoft = Color(0x0091FF24);         // rgba(0,145,255,.14)
    t.accentMutedSurface = Color(0xE3EFFBFF); // light blue background of the outline primary button
    t.accentMutedBorder = Color(0xB9D7F2FF);

    // Neutral surfaces
    t.control = Color(0xE2E2E2FF);
    t.controlHover = Color(0xD8D7D7FF); // element.hover
    t.controlPressed = Color(0xC9C9C9FF);

    // Borders (on light, "weaker" = lighter, closer to the background)
    t.border = Color(0xCBCBCBFF);
    t.borderMed = Color(0xD4D4D4FF);
    t.borderSoft = Color(0xDCDCDCFF);
    t.borderFaint = Color(0xE4E4E4FF);
    t.borderStrong = Color(0xB0B0B0FF); // stronger border on hover (darker)
    t.focusRing = Color(0x0091FFFF);

    // Miscellaneous
    t.knob = Color(0xFFFFFFFF);
    t.textSelection = Color(0xFF840059);     // orange text selection
    t.selection = Color(0xFF840033);         // element.selected — selected row in focus (~0.2)
    t.selectionInactive = Color(0x8D8D8D33); // soft gray — out of focus
    t.rowHover = Color(0x0000000F);          // barely noticeable row hover (~6%)

    // Statuses
    t.success = Color(0x12B69DFF); // created (turquoise)
    t.warning = Color(0xFF8400FF); // orange
    t.danger = Color(0xFF0062FF);  // error (pinkish red)
    return t;
}

} // namespace PandaUI
