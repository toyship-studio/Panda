# PandaUI

A retained-mode UI library for games, editor tools and standalone applications. It is closer to
UIKit / AppKit than to immediate-mode ImGui. Code: `PandaUI/` (structure: `Core`, `Elements`,
`Layout`, `Rendering`, `Runtime`); the demo gallery is `PandaUITest/`.

## Principles and independence

- The UI is a tree of stable objects with lifetime and state. Layout follows the Yoga model.
  Input is events, not polling. Rendering produces draw data, and the backend decides how to draw
  it.
- PandaUI does not depend on Panda, Miren, Fern or Foundation, does not log, and stores abstract
  texture handles. Backend integration lives outside PandaUI.
- **Universality.** Only universal elements go into the library (Button, Toggle, TextField,
  Combobox, TreeView, Menu, Popup and so on). Editor-specific widgets (those that know about
  Entity, Asset or ScriptFieldType) live in `PandaEditor/src/UI/` (namespace `Editor`: panels in
  `UI/Panels/`, widgets in `UI/Widgets/`) and in `PandaEditorKit`. Every new universal primitive
  must have a demonstration in the PandaUITest gallery.

## Structure

- `View` is the base retained object. `Surface` is the visual layer (a light CALayer: background,
  opacity, clip, corner radius, border, shadow). `Control` is the interactive layer (state flags,
  pointer tracking, actions). `Context` is the state of one UI context: it owns the root, the
  overlays, the tooltip and the drag ghost, the layout and the draw list. `Window` is the window
  abstraction over a context. A parent owns its children; lifetime is expressed by the tree.
- Layout: flex, padding, margin, gap, grow, intrinsic sizes. A Label measures text, an ImageView
  its texture, a ScrollView takes contentSize from its content view. Hard-coded sizes are a design
  decision only.
- Text: one `TextLayout` serves Label (`numberOfLines`) and TextField (0 means multiline). Text is
  one of the most important and complex parts of the library.
- Input: pointer events that know their type (mouse, touch, pen), hover, scroll, key, text input,
  focus, pointer capture. Hit testing is in the core; the focus owner is in `Context`, with a Tab
  chain; modifiers travel in `PointerEvent`.
- ScrollView: a real `contentOffset` within bounds plus a separate `overscrollOffset` layer
  (drawOffset is the sum), nonlinear overscroll, a spring return, inertia → bounce. macOS momentum
  events must be told apart and muted where they break the bounce.
- Rendering: primitives rect, rounded rect, border, shadow, text, image, plus `PrimBuffer`
  (drawTriangle, drawGradientRect, drawLine with round caps). Rebuilding all draw commands every
  frame is acceptable; do not complicate things with dirty regions ahead of need.
- The draw list of a frame has a fixed size (`MAX_VERTICES_COUNT` / `MAX_INDICES_COUNT` in
  `Rendering/DrawList.hpp`, asserted when geometry is written): the backend copies it whole into
  the transient buffers of the frame. A view whose geometry grows with the data limits itself.
  The editor's viewport helper lines (collider outlines, tool halos) are such a view: only what is
  in view, detail by the size on screen, and the overlay fills the list at most to a half,
  dropping the tail of the lines, which come ordered by importance.
- Several contexts and windows are supported; the single-window path stays simple.
- Large data: `CollectionView` and `TreeView` are virtualized (only visible cells exist);
  `ListView` is a simple non-virtualized list.

## Overlays: popup and modal

Overlays live in `Context` (drawn over the root and hit-tested first; while an overlay exists Tab
is locked inside the top one and accelerators are silent). There are two primitives with different
contracts:

- **`Popup`** is NON-blocking and anchored (to a button or the cursor point); a click outside or
  Escape closes it. Use it for menus, the combobox list, pickers and quick in-place input. Show it
  with `context->addOverlay(popup)`.
- **`Modal`** is BLOCKING: a dimmer over the whole window plus centered content; the UI under it
  receives no input. A click on the dimmer closes it only with
  `setDismissesOnOutsideClick(true)` (pickers); Escape closes it by default.
  **`Dialog : Modal`** is the standard card "title / message / body / buttons": `setBody(view)`
  takes any form; `addAction(title, role, handler)` calls the handler BEFORE closing, and the
  dialog closes after any action; the roles are `Cancel` (Escape), `Default`, `Primary` and
  `Destructive` (the latter two fire on Enter when the button is enabled). Form validation is
  `setEnabled()` on the main button (returned by `addAction`), not a refusal to close.
- **`ModalManager`** (`Context::modals()`) is the ONLY place that presents modals: `present(modal)`,
  `dismiss`, `dismissAll`, `isPresenting()`, `top()`. It holds the stack and the ownership, so the
  caller keeps no pointer and removes no overlay by hand. Closing a lower modal closes everything
  above it and the popups opened from it (a combobox list inside a dialog form).
  `Modal::dismiss()` is safe from a button callback inside the modal (the overlay is removed
  deferred); `onDismiss` is called once.

Buttons take their size from `ButtonSize` (Regular for forms and dialogs, Small for dense panels,
height 24), not from `layout().setHeight`: fields that do not fit a forced height squash the
header box. A single-line `Label` is centered vertically even when the box is lower than the line
(clamping to the top is kept for multiline text only).

## Integration

- Runtime and editor: the `PandaUIImpl*` bridges map Fern events into PandaUI and pass draw data
  to the Miren path. The bridge is thin and has no hidden logic.
- PandaSDK: scripts work with PandaUI directly as an SDK library (Bamboo does not duplicate its
  concepts; only events, draw data and handles cross the ABI).
  Script bundles are shared libraries: their static SDK graph (`PandaSDK`, `PandaUI`,
  `yogacore`, `PandaAsync`) must be compiled with position-independent code. PandaUI owns its
  CMake target property; the engine root sets it on the vendored `yogacore` target. Rebuild the
  affected archives before packaging the SDK; compiler flags on a script project cannot fix an
  already packaged archive.
- Editor panels follow MVC with explicit Input / Output; the contract is in
  `context/architecture.md`.

## Open items

Anti-aliasing of fills and lines; scrolling of tall modal content (the content is squeezed into
the window today); virtualization of `ListView`; clipboard and IME basics and accessibility
basics (see `context/roadmap.md`).
