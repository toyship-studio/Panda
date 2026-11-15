# The editor

The editor is a tool over the files of a project: every edit made here lands in a text file
(`.world`, `.mat`, `.Panda/project.json`) that reads and diffs like code. This page is what the
hands need to know — the keys and the gestures. Every command of the editor is also in the
command palette, by name, with its keys.

## Finding things

| Keys | What |
| --- | --- |
| ⇧⌘P | **Command palette**: every command of the editor and every item of the menus by name. Type, ↑ ↓, Enter |
| ⌘P | **Go to**: an entity of the open world or an asset of the project by name — it becomes the selection |
| ⌘F | **Find in Hierarchy**: the entities whose name has the text, as a flat list; Escape brings the tree back |

On Windows and Linux ⌘ is Ctrl.

Every "choose one of these" — an asset for a field, an entity for a reference, a component to
add, a script — is the same list with a search field: type to filter, the arrows move the
selection, Enter takes it.

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
| F (or Z) | The camera goes to the selection. A double click on a row of the Hierarchy does the same |
| 1, 2, 3 | The tool of the gizmo: move, rotate, scale |
| F5, ⇧F5 | Play, Stop |
| ⌘Z, ⇧⌘Z | Undo, Redo — of the document whose tab has the keyboard |
| ⌘S, ⌥⌘S | Save the document of the tab, Save All |
| ⌘R | Reload: the editor takes what changed on disk |
| ⇧Space | The panel under the keyboard takes the whole window, and back |

With the keyboard in the viewport W A S D fly the camera; a drag with the right button (two
fingers on a trackpad) looks around, the wheel moves forward and back. A drag with the left
button on empty space selects by a rectangle.

Play needs the scripts of the project. In a project where they were never built, Play offers to
build them; the CMake panel shows the build and the editor loads the result.

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
- **A component section** has a menu at the right of its title: Copy Values, Paste Values,
  Reset, Remove Component. The values travel as text — `{"pointLightComponent": {...}}`, the
  form of the world file — so they paste into another entity or another project, and a copy made
  while the game plays survives Stop: that is how a value tuned in Play reaches the world being
  edited.
- **Add Component** is a list with a search field; a component of a module is filed under the
  name of the module.

Every edit is a step of Undo; a drag is one step.

## The files behind it

- A world is `Assets/*.world`; its settings sidebar (the World button of the viewport) edits
  the `lighting`, `graphics` and `wind` sections of the file ([the format](world-format.md)).
- The settings of the project are `.Panda/project.json` (File → Settings → Open Project
  Settings); the settings of the editor are a JSON file of the user (Open Settings File shows
  it).
- The Output panel says what the editor did and what went wrong, in sentences that name the
  file. The same lines reach a coding agent through the `logs` tool
  ([Working with Zed and coding agents](zed-and-agents.md)).
