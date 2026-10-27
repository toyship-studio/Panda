# The UI of a game

Scripts build their interface with PandaUI directly: the SDK carries the library and its headers,
`#include <PandaUI/PandaUI.hpp>`. Windows, events and textures come through `Bamboo/UI/`. This page
is about making that UI look like your game rather than like the editor. The widgets themselves —
`Label`, `Button`, `TextField`, `Toggle`, `Slider`, `ProgressBar`, `SegmentedControl`, `Stepper`,
`Combobox`, `ListView`, `TreeView`, `CollectionView`, `TabView`, `CollapsibleSection`, `Menu`,
`Dialog`, `Tooltip`, `ScrollView`, `StackView`, `Panel`, `ImageView` — are documented in their
headers under `include/PandaUI/Elements/`.

Three levers, from the strongest to the finest:

| Lever | What it changes | Where |
|---|---|---|
| **Fonts** | The face of every text | `Bamboo::UI::font`, `Label::defaultAppearance().font` |
| **Appearances** | The colors, plates, images and geometry of every widget | `Widget::defaultAppearance()`, `setAppearance` |
| **Surface** | The visual layer of any view: color, gradient, image, corners, border, shadow, clip | `view->surface()` |

## Fonts

A `.ttf`, `.otf` or `.ttc` under `Assets/` is a font asset; a script takes it through a
`FontHandle` field and `Bamboo::UI::font(handle, size)` gives a `PandaUI::Font` (see
`scripting.md`, "Fonts in the UI"). To make it the face of the whole UI, set it once at start,
before the first widget is created:

```cpp
#include <Bamboo/UI/FontAPI.hpp>

const PandaUI::FontFamily family = Bamboo::UI::registerFont(m_uiFont); // a FontHandle field
PandaUI::Label::defaultAppearance().font = PandaUI::Font(16.f, PandaUI::FontWeight::Regular, family);
for (PandaUI::ButtonStyle style : {PandaUI::ButtonStyle::Neutral, PandaUI::ButtonStyle::Accent}) {
    PandaUI::Button::defaultAppearance(style).font = PandaUI::Font(16.f, PandaUI::FontWeight::Bold, family);
}
```

One font file is one weight; a character the face lacks is drawn with the built-in font.

## Appearances

Every widget carries a struct of its look next to its class: `ButtonAppearance`,
`TextFieldAppearance`, `ToggleAppearance`, `SliderAppearance`, `ProgressBarAppearance`,
`SegmentedControlAppearance`, `StepperAppearance`, `ComboboxAppearance`, `ListViewAppearance`,
`TreeViewAppearance`, `CollectionViewAppearance`, `TabViewAppearance`,
`CollapsibleSectionAppearance`, `MenuAppearance`, `DialogAppearance`, `ModalAppearance`,
`TooltipAppearance`, `PropertyRowAppearance`, `LabelAppearance`. Inside: the colors per state, the
plates (`SurfaceStyle`: a fill, a gradient or an image, a border), the fonts and the geometry —
heights, paddings, radii, border widths.

- `Widget::defaultAppearance()` is the look of every instance created **from now on**. Edit it at
  start and the whole game follows; the editor's look is the default, so an untouched game looks
  like the editor.
- `widget->setAppearance(appearance)` restyles one instance.
- `ButtonStyle` is a preset: `Button::defaultAppearance(ButtonStyle::Accent)` is what an accent
  button looks like; `ButtonSize` lays its metrics (`Button::defaultMetrics(size)`) over it.
- Colors are `ThemeColor`: a theme token (`ThemeColor::accent`) follows the theme, a literal
  (`Color(0xC9A227FF)`) is yours; `ThemeColor::withShade(0.15f)` lightens a token for a hover
  tone, a negative value darkens it. A token is the only way to name a color of the theme; when a
  plain `Color` is needed right now, in your own `draw()` for example, resolve it:
  `ThemeColor::textDim.resolve()`. `Theme::current()` is the whole theme as values, for a color
  derived from several, not for storing.

```cpp
using namespace PandaUI;
ButtonAppearance plate = Button::defaultAppearance(ButtonStyle::Neutral);
const EdgeInsets caps(18.f, 18.f, 18.f, 18.f); // the corners of the image that keep their size
plate.normal.surface = SurfaceStyle(Color(0.f, 0.f, 0.f, 0.f)).withImage(SurfaceImage(m_plate, caps));
plate.hovered.surface = SurfaceStyle(Color(0.f, 0.f, 0.f, 0.f)).withImage(SurfaceImage(m_plateHovered, caps));
plate.pressed.surface = SurfaceStyle(Color(0.f, 0.f, 0.f, 0.f)).withImage(SurfaceImage(m_platePressed, caps));
plate.normal.text = Color(0xF2E2B8FF);
plate.font = Font(17.f, FontWeight::Bold, family);
plate.padding = EdgeInsets(8.f, 22.f, 22.f, 8.f);
plate.minHeight = 40.f;
plate.cornerRadius = CornerRadii(0.f); // the image has its own shape
plate.borderWidth = 0.f;
plate.titleShadow = TextShadow(Point(0.f, 1.f), Color(0.f, 0.f, 0.f, 0.6f));
Button::defaultAppearance(ButtonStyle::Neutral) = plate; // every neutral button from now on
```

## Surface: the layer of a view

`view->surface()` is what CALayer is to a UIView; every view has one, a `Panel` is nothing but
one.

- **Fill**: `setBackgroundColor(color)`, or `setBackgroundGradient(SurfaceGradient(start, end,
  GradientDirection::TopToBottom))`.
- **Image**: `setBackgroundImage(SurfaceImage(texture, capInsets, tint))` over the fill. Zero
  insets stretch the texture; insets in texture pixels make nine slices — the corners keep their
  size, the edges stretch along one axis, the center along both (a frame, a plate, a HUD bar from one
  small image). `SurfaceImage::textureCoords` picks a part of an atlas, `scale` is texture pixels
  per point for a @2x image.
- **Corners**: `setCornerRadius(r)` or `setCornerRadii(CornerRadii::top(r))` and the like per
  corner.
- **Border** inside the bounds: `setBorderWidth`, `setBorderColor`. **Shadow**:
  `setShadow(Shadow::popover)` or your own `ShadowStyle`. **Opacity**: `setOpacity`.
- **Clip**: `setClipsToBounds(true)` keeps the subviews inside; with a corner radius the clip is
  rounded too (a list inside a rounded card ends at the corners).

Text has its own decorations on `Label`: `setTextOutline(TextOutline(1.5f, color))` and
`setTextShadow(TextShadow(offset, color))` — HUD text over any scene.

## Textures for the UI

`Bamboo::UI::createTextureRGBA8(pixels, width, height)` (`Bamboo/UI/TextureAPI.hpp`) makes a UI
texture from pixels; `Bamboo::TextureAPI::readPixelsRGBA8` (`Bamboo/Assets/TextureAPI.hpp`) reads
the pixels of a texture asset, so a plate drawn in an image editor and dropped into `Assets/`
becomes a nine-slice through a `TextureHandle` field. `destroyTexture` frees one; everything goes
with the scripts on Stop.

## The gallery

The PandaUI gallery (`PandaUI/examples/gallery`; in the editor **Help → UI Gallery**) shows every
element of the library with a live example under the theme in use. Its section **Skins** shows the
same controls with the editor look and with a "Fantasy" skin — nine-slice plates drawn at start, a
serif face of the system, outlines, gradients and a rounded clip — built only with the means above
(`PandaUI/examples/gallery/src/Sections/SkinsSection.cpp`).
