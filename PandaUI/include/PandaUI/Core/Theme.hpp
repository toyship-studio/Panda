#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cstdint>
#include <functional>

namespace PandaUI {

// Semantic color scheme of the UI. Controls and applications read colors from here instead of
// hardcoding them. The theme can be changed at runtime (setTheme), e.g. from a menu/scheme
// picker window or from an engine plugin; PandaUI notifies the contexts and re-reads the colors.
struct Theme {
    // Surfaces (backgrounds) — a layered background in the spirit of Zed: different tones for
    // chrome / panels / fields.
    Color windowBackground; // bg/app     #141414 — base editor background, panels
    Color surface;          // bg/toolbar #1E1E1E — toolbar, menus/popups, dialogs, active tab
    Color surfaceVariant;   // bg/chrome  #191918 — menu bar, tabs, strips, focused TextField
    Color input;            // bg/input   #1B1B1B — input fields, X/Y/Z cells, asset previews
    Color inputDeep;        // bg/input-2 #161616 — search/command line, panel status bars

    // Text — brightness ramp per the design: primary > secondary > muted > dim > disabled
    Color textPrimary;   // #E6E6E6 — main text, active values
    Color textSecondary; // #cfcfcf — regular rows, secondary but readable content
    Color textMuted;     // #969696 — field labels, icons/glyphs at rest, counters
    Color textDim;       // #6e6e6e — captions, placeholders, metadata, chevrons
    Color textDisabled;  // #595959 — inactive/hidden entities

    // Accent (primary action)
    Color accent;             // #45A9F9
    Color accentHover;        // #6FC1FF
    Color accentPressed;      // derived dark
    Color onAccent;           // #06121f — dark text on a solid accent button
    Color accentSecondary;    // #12B69D — secondary accent (e.g. the selected SegmentedControl segment)
    Color accentText;         // #9ecbff — text/icons on an accent backing, outline-primary
    Color accentSoft;         // rgba(69,169,249,.16) — background of the active tab/icon/toggle
    Color accentMutedSurface; // #1f2a36 — background of the outline primary button (Add Component)
    Color accentMutedBorder;  // #2c4156 — border of the outline primary button

    // Neutral interactive surfaces (icon hover, neutral button)
    Color control;        // bg/hover   #2B2B2B
    Color controlHover;   // bg/hover-2 #353637
    Color controlPressed; // #232323

    // Borders / separators / focus — ramp strong > med > soft > faint; borderStrong = hover emphasis
    Color border;       // border/strong #303030 — between large zones (toolbar, panels)
    Color borderMed;    // border/med    #2a2a2a — input fields, cards
    Color borderSoft;   // border/soft   #262626 — panel header separators
    Color borderFaint;  // border/faint  #1f1f1f — row separators (lists, log, tables)
    Color borderStrong; // #3a3a3a — strengthened field border on hover
    Color focusRing;    // #6FC1FF

    // Other
    Color knob;             // toggle knob / slider handle
    Color textSelection;    // semi-transparent text selection highlight
    Color selection;        // selected row/cell background when the list is focused (soft orange ~0.14)
    Color selectionInactive; // same, when the list is NOT focused (soft gray)
    Color rowHover;          // barely visible highlight of the row/cell under the cursor (~0.05)

    // Statuses
    Color success;
    Color warning;
    Color danger;

    static Theme dark();
    static Theme light();
};

const Theme &theme();
void setTheme(const Theme &theme);
// Grows with every setTheme. A view remembers the generation it was styled under: one that was
// out of the tree during a change (a hidden root, a replaced tab) restyles itself when attached.
uint64_t themeGeneration();

// The runtime layer installs a handler to update the contexts when the theme changes: PandaUI Core
// does not depend on Context, so the link goes through this hook (as with the clipboard).
void setThemeChangedHandler(std::function<void()> handler);

} // namespace PandaUI
