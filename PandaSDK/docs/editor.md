# The editor

The editor is a tool over the files of a project: every edit made here lands in a text file
(`.world`, `.mat`, `.Panda/project.json`) that reads and diffs like code. This page is what the
hands need to know — the keys and the gestures. Every command of the editor is also in the
command palette, by name, with its keys; the keys on this page are the defaults, and every one
of them can be changed ([Keymap](#keymap)).

The editor follows the files it works on: an edit made in another program — a text editor, an
agent, git — is taken as soon as the editor is the application in front, without a command. A
document with unsaved edits is left as it is; saving it asks which version stays.

## Finding things

| Keys | What |
| --- | --- |
| ⇧⌘P | **Command palette**: every command of the editor and every item of the menus by name. Type, ↑ ↓, Enter |
| ⌘P | **Go to**: an entity of the open world or an asset of the project by name — it becomes the selection |
| ⌘F | **Find in Hierarchy**: the entities whose name has the text, as a flat list; Escape brings the tree back |
| ⌘, | **Settings**: a window over the editor — the editor, the project and the keymap. Escape closes it |
| ⌘K | **Keymap**: the same window on its Keymap page |

On Windows and Linux ⌘ is Ctrl.

Every "choose one of these" — an asset for a field, an entity for a reference, a component to
add, a script — is the same list with a search field: type to filter, the arrows move the
selection, Enter takes it. Escape in a search field clears what you typed; Escape in an empty
one closes the list.

## The world

These keys act on the selected entities while the keyboard is in the viewport or in the
Hierarchy (a click there gives it):

| Keys | What |
| --- | --- |
| Delete, ⌫ | Delete |
| ⌘D | Duplicate |
| ⌘C, ⌘X, ⌘V | Copy, Cut, Paste. The clipboard holds text — a fragment of a world file — so entities paste into another world, another project, a text editor |
| F2 | Rename |
| ⌘A, Escape | Select all, deselect |
| F | The camera goes to where the selection fits the frame. A double click on a row of the Hierarchy does the same |
| 1, 2, 3 | The tool of the gizmo: move, rotate, scale |
| F5, ⇧F5 | Play, Stop |
| ⌘B | Build the scripts of the project: the CMake panel shows the build, the editor loads the result |
| ⌘Z, ⇧⌘Z | Undo, Redo — of the document whose tab has the keyboard |
| ⌘S, ⌥⌘S | Save the document of the tab, Save All |
| ⇧Space | The panel under the keyboard takes the whole window, and back |
| ⌘W | Close the tab the keyboard is in |

The Hierarchy lists the entities of a level — the roots, the children of an entity — the ones
with children first, then by name: there is no order to arrange by hand, and the world file is
written in the same order.

With the keyboard in the viewport W A S D fly the camera; a drag with the right button (two
fingers on a trackpad) looks around, the wheel moves forward and back. A click selects the
entity under the pointer; a drag with the left button selects by a rectangle. The inspector of
what you selected opens when the button is released.

A script of an entity is a section of its inspector with the fields the class publishes
(`PANDA_FIELD`). A list field (a `std::vector` in the class) is a block of rows: **+** in its
header adds an item, the bin of a row removes it, and a row dragged by its index onto another
takes that place. An asset, an entity or a whole folder of the Content Browser dropped on the
header becomes items of the list. A class that
publishes none says so, a class the last script build does not have is marked, and the link
under the section opens the source of the class in your text editor (Settings → Editor →
External Tools).

A click on a world file in the Content Browser shows that world in the preview tab (its title
is in italics): the next world clicked takes the same tab. A double click on the file or on the
tab keeps it open, and so does the first edit. The tabs of the worlds can be dragged into
another order. A world tab closes like any other tab; with the last one closed the center is
empty — the "+" in the middle makes a new world, a click on a world file opens one.

The Content Browser shows the `Assets` folder of the project as it is on disk, the names in the
order a person reads them (`level2` before `level10`). A right click on the empty space makes a
folder or an asset in the folder shown; a right click on a folder opens it, makes something
inside it (New), or deletes it with everything in it — the editor asks first, and the files do
not come back with Undo. An asset is dragged from here onto a field that takes it and into the
viewport; a folder is dragged onto the header of a list field of a script and gives it every
asset of the kind of the list.

Play needs the scripts of the project. In a project where they were never built, Play offers to
build them; the CMake panel shows the build and the editor loads the result. Build Scripts
(Cmd/Ctrl+B) builds them at any time; a build that ends while the game plays is loaded when the
game stops.

The Scene (the tab of a world: the editor camera, the selection, the gizmo) and the Game (the
main camera of the world, the game UI and the game input) are not drawn together: the editor
draws the one you work in. With both on screen — the Game tab dragged into another dock — a
click on a viewport gives it the focus and it renders; the other one keeps its last picture
under a light gray veil with a caption that says who has the focus. Play gives the focus to the
Game, Stop gives it back to the Scene. While the Scene has the focus during Play the game goes
on — it is simulated and its UI lives — only its picture waits for the focus to return.

The CMake panel builds the C++ scripts of the project: **Configure** runs cmake again
(after a source file was added), **Build** compiles, **Clean** deletes the build folder so that
the next Build starts from nothing, **Stop** stops the build that runs. The game itself is
exported in a window of its own, **File → Export Game** ([Exporting a game](export.md)). What the scripts are
built with — the cmake, the generator (Ninja, Makefiles), the C and C++ compilers — is the
Toolchain section of the editor settings (the Toolchain button of the panel opens it); everything
is found on its own until you choose otherwise, and a change of the generator or of a compiler
cleans the build folder by itself. A file and a line that the output names — an error of the
compiler, a warning of cmake — are a link: a click opens the file there in the external editor
([Working with Zed and coding agents](zed-and-agents.md), "One file, one tab").

## Previews

The tab of a material or a model has a little viewport at its top. It is turned the way the
Scene is:

- a two-finger swipe of the trackpad, or a drag with a mouse button, orbits the camera around
  the thing;
- the wheel, or a pinch, moves closer and farther;
- a double click returns to the first view.

A swipe that begins over the preview turns it; one that begins over the form scrolls the form,
also when the preview passes under the fingers. A material is shown on a sphere; the menu in the
corner of its preview puts it on a cube, a cylinder, a capsule or a plane. The Content Browser
and the asset fields show the same pictures small: a material, a model and a texture have a
thumbnail of their own.

## Forms

The inspector, the settings and the asset tabs are made of the same rows.

- **A number** is typed into its field, or changed by dragging: the label of its row is a handle
  (the cursor shows it), and so is the X / Y / Z letter of a vector. Shift makes the drag ten
  times coarser, Alt ten times finer. In a field, ↑ and ↓ step the value the same way. A double
  click on an axis letter resets that component (to 1 for a scale).
- **The label of a row tells what the field is**: hold the pointer over it.
- **A row that does not apply now is not shown**: the cutoff of a material that is not a cutout,
  the softness of a lamp without a cone.
- **A reference field** (an asset, an entity) is `[value | choose | clear]`: a click on the
  value shows where the thing is, a double click selects it, an empty field opens the list, and
  a drop from the Content Browser or the Hierarchy sets it.
- **The form of a selection is a preview tab** (its title is in italics): the next thing
  selected takes the same tab. A double click on the tab keeps it open, and so does the first
  edit of an asset. A form that is kept stays while something else is selected — an entity
  whose list is to be filled, with several assets selected in the Content Browser and dragged
  onto it at once.
- **A component section** has a menu at the right of its title: Copy Values, Paste Values,
  Reset, Remove Component. The values travel as text — `{"pointLightComponent": {...}}`, the
  form of the world file — so they paste into another entity or another project, and a copy made
  while the game plays survives Stop: that is how a value tuned in Play reaches the world being
  edited.
- **Add Component** is a list with a search field; a component of a module is filed under the
  name of the module.

Every edit is a step of Undo; a drag is one step.

## Settings

File → Settings → Open Settings (⌘,) opens the Settings window. It stays over the editor without
covering the viewport's meaning: what is changed shows in the scene behind it.

- **Editor** — this machine: the theme, the fonts, the viewport, the external editor. Applied
  and saved at once.
- **Project** — `.Panda/project.json`: the start world, the rendering features, the modules.
  Applied when saved: a bar with Save and Revert appears while there are unsaved changes, and
  ⌘S and ⌘Z act on this page while the window has the keyboard.
- **Keymap** — the keys.

The settings of a world are not here: they belong to the world file and live in the sidebar of
the viewport (the World button).

## Keymap

The Keymap page lists every command with its keys, where the key works (the entity commands —
only with the keyboard in the viewport or the Hierarchy) and whether the binding is the default
or yours. Type in the search field to find a command by its name or by a key. A double click on
a command (or Change Keys…) opens the recorder: press the keys, then Enter. If the keys already
run another command, the recorder says which; both stay bound, and the one that applies takes
the key. Reset returns the default.

The changes are a text file, `keymap.json`, next to the editor's `settings.json` (File →
Settings → Open Keymap File): a command id and its chords, only what differs from the defaults.

```json
{
    "camera.focusSelection": ["f", "z"],
    "world.play": ["cmd-enter"],
    "entity.rename": []
}
```

A chord is modifiers and a key joined by `-`: `cmd`, `ctrl`, `alt`, `shift`, then a letter, a
digit, `f1`…`f25`, `space`, `enter`, `escape`, `tab`, `backspace`, `delete`, `up`, `down`,
`left`, `right`, `home`, `end`, `pageup`, `pagedown` or a punctuation key. Several chords for
one command are alternatives; an empty list unbinds it. The ids are the ones the Keymap page
and the command palette show next to the names (`command` of the MCP server lists them).

## The files behind it

- A world is `Assets/*.world`; its settings sidebar (the World button of the viewport) edits
  the `lighting`, `graphics` and `wind` sections of the file ([the format](world-format.md)).
- The settings of the project are `.Panda/project.json`; the settings of the editor and the
  keymap are JSON files of the user (File → Settings → Open Settings File, Open Keymap File).
- The Output panel says what the editor did and what went wrong, in sentences that name the
  file. The same lines reach a coding agent through the `logs` tool
  ([Working with Zed and coding agents](zed-and-agents.md)).
