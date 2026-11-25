# PandaUI

A retained-mode UI library for games, editor tools and standalone applications. It is closer to
UIKit / AppKit than to immediate-mode ImGui. Code: `PandaUI/` (structure: `Core`, `Elements`,
`Layout`, `Rendering`, `Runtime`); the gallery is `PandaUI/examples/gallery/` — a library of pure
PandaUI the editor shows in a tab (Help → UI Gallery) and the standalone example runs on a backend.

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
  must have a demonstration in the gallery.

## Structure

- `View` is the base retained object. `Surface` is the visual layer (a light CALayer: background,
  opacity, clip, corner radius, border, shadow). The opacity is of the view with everything in
  it — its text or picture and its subviews: `View::render` pushes it onto the renderer
  (`UIRenderer::pushOpacity`), which scales the alpha of every primitive drawn until the pop
  (shapes that overlap inside the view show through each other: the view is not composited as
  one picture). Fonts: a context resolves family × weight (`Context::getFontAtlas`); a pair that
  is not there is asked of the host once (`setFontLoader`) before it falls back — the UI of a
  game rasterizes the weights of its default font as its texts first use them, at 40 px on a
  dense screen and 20 px on a plain one (`Bamboo::UI::WindowRegistry`); the editor registers all
  of its faces at its start. `Control` is the interactive layer (state flags,
  pointer tracking, actions). `Context` is the state of one UI context: it owns the root, the
  overlays, the tooltip and the drag ghost, the layout and the draw list. `Window` is the window
  abstraction over a context. A parent owns its children; lifetime is expressed by the tree.
- Layout: flex, padding, margin, gap, grow, intrinsic sizes. A Label measures text, an ImageView
  its texture, a ScrollView takes contentSize from its content view. Hard-coded sizes are a design
  decision only.
- Text: one `TextLayout` serves Label (`numberOfLines`) and TextField (0 means multiline). Text is
  one of the most important and complex parts of the library. A line ends at a space, or after a
  joint of a name or a path (`_`, `/`, `\`, a hyphen not before a digit, a point before a letter:
  `blocks_ | albedo.png`, `nightstand. | gltf`), the mark staying on the line; a word with no
  such place is cut where the line is full.
- Input: pointer events that know their type (mouse, touch, pen), hover, scroll, key, text input,
  focus, pointer capture. Hit testing is in the core; the focus owner is in `Context`, with a Tab
  chain; modifiers travel in `PointerEvent`. A `ScrollEvent` is a wheel, a two-finger swipe of a
  trackpad (`isTrackpad`) or a pinch (`magnification`: the share the fingers moved apart by; the
  delta is zero then). A swipe belongs to the view that took its first event
  (`Context::handleScroll`): what moves under the pointer while it lasts — a form scrolling a
  viewport under the fingers — does not take the rest of it.
  A finger scrolls a list from anywhere in it: the press goes to what is under the finger at
  once (a button highlights), and once the finger moved 10 points (`TOUCH_SLOP`) the views from
  it up to the root are offered the finger, nearest first (`View::takesTouch`) — a `ScrollView`
  that can go the way the finger went takes it, and what had the press gets
  `interactionCancelled` (a button lets go without its action). So a row that only goes sideways
  leaves an up-and-down drag to the list around it. The list wins over a button on purpose: a
  plain `UIScrollView` leaves the finger to a control that already has it
  (`delaysContentTouches`, `touchesShouldCancel(in:)`), and a list of buttons scrolls there only
  from a quick swipe. A view that works with the moving finger keeps it (`View::keepsTouch`: a
  slider, a text field selecting, the color picker, a split divider) whichever way the finger
  goes, so a slider or a text field in a list on a touch screen holds an up-and-down drag too
  (UIKit hands the view the gesture, `gestureRecognizerShouldBegin(_:)`, and its slider refuses
  only a swipe along it from the thumb); when such a list comes, `keepsTouch` gets the movement
  as `takesTouch` does. A finger that comes down on a list
  still moving (a point a frame or faster) stops it and presses nothing. A mouse drags a list
  only from its background, at once, as before. `PandaSmoke` ("a finger in a scroll view")
  holds these rules.
- TabView: `setEmptyView` is what the content area shows while there is no tab (a hint, a way
  to open something). A tab is dragged within its bar, to any bar of its drag group, or not at
  all (`TabDrag`). Choosing a tab gives the keyboard focus to its content
  (`focusSelectedContent`; a click on a tab does it), and coming back to a tab is coming back to
  the place: the focus goes to the view of that content that had it last
  (`View::lastFocusedInside` — the context keeps it for every view above the one that takes the
  focus, so it stays with the content wherever that is moved), and nothing is scrolled. A tab
  that did not have the focus yet gives it to the first view that takes it, to a text field only
  when there is nothing else: choosing a tab is not the wish to type. A content that is to take
  the focus as a whole — a form, a panel that only shows — is focusable itself.
- ScrollView: a real `contentOffset` within bounds plus a separate `overscrollOffset` layer
  (drawOffset is the sum), nonlinear overscroll, a spring return, inertia → bounce. macOS momentum
  events must be told apart and muted where they break the bounce. A scroll view that is not laid
  out — the content of a tab that is not the selected one has no size — measures nothing and
  keeps its offset: it is where it was when it is shown again. Its content lies over it and does
  not count for its size (a panel fills the room its parent gives). `setSizesToContent(true)`
  turns that around for one axis, as a box of a web page with `overflow: auto`: the content takes
  part in the layout (the view is a Yoga scroll box: the content is measured at its whole length),
  the view is as long as it and gives way (flexShrink 1) to a parent with less room or a max size
  of the view itself — only what does not fit scrolls. A popover or a dialog of a length nobody
  knows in advance is made of it: the card of the Display popover has a max height of 100% of
  its popup, that is the window.
- Rendering: primitives rect, rounded rect, border, shadow, text, image, plus `PrimBuffer`
  (drawTriangle, drawGradientRect, drawLine with round caps). Rebuilding all draw commands every
  frame is acceptable; do not complicate things with dirty regions ahead of need.
- The draw list of a frame has a fixed size (`MAX_VERTICES_COUNT` / `MAX_INDICES_COUNT` in
  `Rendering/DrawList.hpp`, asserted when geometry is written): the backend copies it whole into
  the transient buffers of the frame. A view whose geometry grows with the data limits itself.
  The editor's viewport helper lines (collider outlines, tool halos) are such a view: only what is
  in view, detail by the size on screen, and the overlay fills the list at most to a half,
  dropping the tail of the lines, which come ordered by importance.
- Forms: `PropertyRow` is a label and an editor. A filling editor (a field) starts at a fixed
  label column, so the fields of neighboring rows align; an editor that keeps its own size is
  placed by what it is — a `Toggle` goes to the right edge and the label takes the rest of the
  row (`PropertyRowPlacement::Trailing`), anything else stays at the start of the field column.
  A row may carry a description under its title and a tooltip over it. A row whose editor is a
  `NumberField` scrubs it: the label is a drag handle (`NumberField::beginScrub` / `scrub` /
  `endScrub`; a pixel is a hundredth of the value, never more than a hundredth of the range,
  and the value lands on round numbers). `NumberField::setStep(1)` makes a field of whole numbers.
  A number field reads arithmetic as Unity, Godot and Blender do — "2*3", "(1 + 0.5) / 4", a
  comma as a decimal point ("1,5") — and takes the value when the expression is whole
  (`NumberField::evaluate`; `PandaSmoke`, "arithmetic in a number field").
- `View::setHidden` keeps the place of a view in the layout; `setCollapsed` takes it out.
- A tree opens and closes a whole branch with Alt (Option) — a click on the arrow or ←/→, as in the
  Finder (`TreeView::setBranchExpanded`).
- `ListView` tells a row that was chosen (`setOnItemActivated`: a click, `activateSelected`)
  from a selection that only moved (the arrows): a palette picks on the first.
- Several contexts and windows are supported; the single-window path stays simple.
- Large data: `CollectionView` and `TreeView` are virtualized (only visible cells exist);
  `ListView` is a simple non-virtualized list. One cell whose data changed is bound again without
  a reload — `CollectionView::reconfigureItem(index)`, `TreeView::reconfigureItem(item)` (a
  picture that arrived for a row on screen). The rows of a `TreeView` are of one height, which may
  change while it shows its data (`setRowHeight`: a list the user zooms) — the row at the top
  stays at the top, the rows on screen are bound again, the model is not walked.
- A `Combobox` whose list is longer than its dropdown shows (`maxVisibleRows`) is searched: the
  dropdown opens with a search field that has the keyboard — typing filters the choices (one that
  starts with the text first, then one that has it), the arrows walk them, Enter takes one, the
  first Escape takes the text back. The card keeps the size of the whole list while it is filtered:
  the field does not move under the typing, and a dropdown that opened above the box (no room
  below) does not jump below it. The pickers of the editor keep their size the same way.
- Files dropped onto the window from the file manager are a drop of the type "files" with their
  paths (`DragPayload::files`): `Context::dropFiles` gives it to the nearest view under the point
  that takes it (`canAcceptDrop` / `performDrop`); a host on Fern calls it for `FilesDropped`.
  `CollectionView::indexAt` and `TreeView::itemAt` name the item under a point of the window.
- A view that takes a drop INTO an item (a folder) marks it while the drag is over it:
  `CollectionView::setDropHighlightIndex`, `TreeView::setDropHighlightItem` draw the drop border
  of the appearance (`dropBorder`, `dropBorderWidth`) around that cell or row; -1 (null) takes it
  away. What is a target is decided by the view that takes the drop — the collection only draws.
- A tree whose drag payload depends on what is dragged — files of several kinds — fills it itself
  (`TreeDataSource::fillDragPayload`, as `CollectionViewDataSource::fillDragPayload`); the tree is
  still the source, and it reparents only a payload of its own drag type.

## Overlays: popup and modal

Overlays live in `Context` (drawn over the root and hit-tested first; while an overlay exists Tab
is locked inside the top one and accelerators are silent). There are two primitives with different
contracts:

- **`Popup`** is NON-blocking and anchored (to a button or the cursor point); a click outside or
  Escape closes it. Use it for menus, the combobox list, pickers and quick in-place input. Show it
  with `context->addOverlay(popup)`. A text field inside may keep the first Escape
  (`TextField::setClearsOnEscape`: with text it clears the text and consumes the key, empty it
  lets the key go up) — a search field is cleared before its popup closes.
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

## Elements: what a UI test sees

A UI test does not look at a picture and does not click at coordinates: it finds a control by
what it is and reads what it shows. PandaUI gives that the way UIKit does — an identifier, a
label, a value and the traits — and nothing of it costs a view that is not asked.

- **What a view says about itself** (`Core/Accessibility.hpp`). `View::describeAccessibility`
  fills a `role` ("button", "textField", "toggle", "property", "section", "tab", "row", "cell",
  "menuItem", "dialog", …), a `label` (the text it shows: the title of a button, of a row; a
  button of an icon alone goes by its tooltip), a `value` (the text of a field, "on" / "off",
  "expanded" / "collapsed", the number of rows of a tree), `selected`, and `atomic` — its subviews
  are its parts (the title of a button), not elements of their own. Every control of the library
  describes itself; a new control overrides the one method. A view without a role is a plain
  container and is not an element.
- **The id** (`View::setAccessibilityId`) is the `accessibilityIdentifier` of UIKit: a name the
  code that builds a screen gives a view, shown nowhere. It is for what a label cannot tell
  apart or does not name: the three fields of a vector ("X", "Y", "Z"), a panel, a row whose
  label is a number. A view that was only given an id is an element of the role "view".
- **The tree** (`Runtime/AccessibilityTree.hpp`). `accessibilityTree(context)` is a snapshot of
  everything the context shows, the lowest layer first (`Context::layers`: the root, the
  overlays — popups, menus, modals — the tooltip). Plain containers are not in it: the elements
  inside one stand in its place. An element carries its frame and the part of it a press can
  reach (`visible`: what the frames of the views above leave — the rule of `View::hitTest`).
  Hidden views are not elements.
- **A path** names elements in a tree (`findAccessibilityElements`): segments separated by "/",
  each one an element inside the one before it at any depth. A segment is an id; where nothing
  there has such an id, a label; else a role. An element inside another one the same segment
  names is the same thing said twice (the title label of a row) — the outer one counts.
  `role:name` and `name[2]` tell apart what a name alone does not. A path that names nothing
  comes back with how far it got and where the next segment was looked for, so the caller can
  say what is there.
- **Rows made on demand.** `TreeView` and `CollectionView` make only the rows in sight and reuse
  them, so a row is named by the data, not by the view: `titleForItem` of the data source is the
  label of the row that shows the item, and `View::revealAccessibilityElement(name)` scrolls to
  an item that has no row yet. `ScrollView::scrollToVisible` brings a view of its content into
  sight. A caller that wants to press an element reveals it, waits for it to stand still, and
  asks `Context::viewAt` whether the press reaches it.
- The library only describes. Who reads the description is outside: the editor's MCP server
  answers with the tree and plays gestures at elements (`context/architecture.md`, "Editor: the
  MCP server"). Nothing hands it to the assistive technology of a system yet.

## Integration

- PandaUI knows neither Fern nor Miren; the engine joins them once, for every host of a context
  (`Panda/include/Panda/UI/`). `PandaUIFern::translate` turns an event of a window into a
  `PandaUI::Event` — which `Context::handleEvent` dispatches — and maps the cursor and the
  clipboard; `PandaUIMirenRenderer` holds the two UI programs and the textures behind the PandaUI
  handles and submits a draw list into a view. On top of the pair there are two hosts and no
  third: `PandaUIFernHost` for a context that lives in the process (the editor's `EditorUI`, any
  tool built on the engine), and `GameUI` for the UI of a game, whose contexts live in the script
  bundle and reach the same pair through the `ui_*` entries of the script ABI.
- The gallery is pure PandaUI too. What it needs from an application — textures from pixels, the
  logo image, a serif face for its "Fantasy" skin, extra windows, a theme switch — comes through
  `PandaUI::Gallery::Host` (`examples/gallery/include/PandaUIGallery/Gallery.hpp`); a capability
  the host does not give hides the sections that need it. The editor gives textures and the face
  only: the gallery is a tab of its window and follows the theme chosen in Plugins.
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
the window today); virtualization of `ListView`; clipboard and IME basics; accessibility for
people — the elements exist ("Elements: what a UI test sees"), a bridge to the screen reader of a
system does not (see `context/roadmap.md`).
