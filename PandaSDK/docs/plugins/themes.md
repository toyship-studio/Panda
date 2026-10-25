# Themes

A theme is one JSON file in `data/themes/`. It names a base — the built-in dark or light palette —
and overrides any of its colors; what it does not name keeps the value of the base, so a theme can
be two lines long.

```json
{
    "name": "Sunset",
    "base": "dark",
    "colors": {
        "accent": "#F9A445",
        "accentHover": "#FFBE6F",
        "accentText": "#FFD9A8",
        "accentSoft": "#F9A44529",
        "windowBackground": "#161311",
        "surface": "#201B18",
        "surfaceVariant": "#1B1613"
    }
}
```

- `name` — the label of the button on the plugin card and in the APPEARANCE section of the editor
  settings.
- `base` — `dark` or `light`.
- `colors` — field names of the editor theme (the table below) with hex colors: `#RRGGBB`,
  `#RRGGBBAA`, or the short `#RGB` and `#RGBA`. An unknown name is a warning in the Output panel
  and is ignored.

The id of a theme is `<plugin id>/<file name without .json>`: it is what the editor remembers as
the chosen theme, so renaming the file resets the choice of everyone who uses it. Several files
in the folder are several themes of one plugin, listed in the alphabetical order of the files.

## The colors

The dark values are the built-in Panda Dark theme; the light base has its own values for every
field.

| Field | Dark | Used for |
|---|---|---|
| `windowBackground` | `#141414` | the base background of the editor and the panels |
| `surface` | `#1E1E1E` | the toolbar, menus and popups, dialogs, the active tab, cards |
| `surfaceVariant` | `#191918` | the menu bar, tab strips, a focused text field |
| `input` | `#1B1B1B` | input fields, X/Y/Z cells, asset previews |
| `inputDeep` | `#161616` | the search field, panel status bars |
| `textPrimary` | `#E6E6E6` | main text, active values |
| `textSecondary` | `#CFCFCF` | regular rows, readable secondary content |
| `textMuted` | `#969696` | field labels, icons at rest, counters |
| `textDim` | `#6E6E6E` | captions, placeholders, metadata, chevrons |
| `textDisabled` | `#595959` | inactive or hidden entities |
| `accent` | `#45A9F9` | the primary action: buttons, the active segment, links |
| `accentHover` | `#6FC1FF` | the accent under the cursor |
| `accentPressed` | derived | the accent while pressed |
| `onAccent` | `#06121F` | text on a solid accent button |
| `accentSecondary` | `#12B69D` | the secondary accent (the selected segment of a segmented control) |
| `accentText` | `#9ECBFF` | text and icons on an accent backing, outline buttons |
| `accentSoft` | `rgba(69,169,249,.16)` | the backing of the active tab, icon or toggle |
| `accentMutedSurface` | `#1F2A36` | the background of an outline primary button |
| `accentMutedBorder` | `#2C4156` | the border of an outline primary button |
| `control` | `#2B2B2B` | neutral buttons, icon hover, tags |
| `controlHover` | `#353637` | a neutral control under the cursor |
| `controlPressed` | `#232323` | a neutral control while pressed |
| `border` | `#303030` | borders between large zones (toolbar, panels) |
| `borderMed` | `#2A2A2A` | input fields, cards |
| `borderSoft` | `#262626` | panel header separators |
| `borderFaint` | `#1F1F1F` | row separators in lists, logs and tables |
| `borderStrong` | `#3A3A3A` | the border of a field under the cursor |
| `focusRing` | `#6FC1FF` | the focus ring |
| `knob` | | the toggle knob and the slider handle |
| `textSelection` | | the text selection highlight (semi-transparent) |
| `selection` | | a selected row or cell while the list has the focus |
| `selectionInactive` | | the same when the list has no focus |
| `rowHover` | | the row or cell under the cursor (barely visible) |
| `success` | | status: success |
| `warning` | | status: warning |
| `danger` | | status: danger, destructive buttons |

The fields are the members of `PandaUI::Theme` (`PandaUI/include/PandaUI/Core/Theme.hpp`), and
the defaults are in `Theme::dark()` and `Theme::light()` next to it.

## Working on a theme

Install the folder as a dev plugin, press the theme's button on the card, then edit the file and
press Reload: the whole editor recolors at once. Keep the Plugins tab, a settings tab and the
viewport open while you work — together they use every color of the table. A theme that is used
by someone is changed with care: a removed file resets their choice to the built-in dark theme
until the id exists again.
