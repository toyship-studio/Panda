# Icons

An icon plugin replaces editor icons by their names. `data/icons/<name>.png` takes the place of
the icon called `<name>` everywhere in the editor — the toolbar, the panel headers, the tabs, the
hierarchy, the content browser — the moment the plugin is installed or reloaded, and the original
comes back when the plugin is removed. A plugin may replace one icon or all of them.

## The files

- PNG, named by the icon token: `save.png`, `panel-hierarchy.png`. At most 256 KB each.
- White alpha masks: the shape is the alpha channel, the color channels are white. The editor
  colors the icon itself (muted at rest, accent when active), so a colored PNG loses its colors.
- The canvas of the originals: 48×48 for the editor icons, 168×168 for the file previews
  (`file-*`, `asset-world`, `prefab`). Draw on the same canvas with the same margins as the
  original to keep the toolbar even — open the originals from the repository
  (`PandaEditor/resources/ui/editor-icons/`, `ui/file-icons/`) and use them as the template.
- A name the editor does not have is accepted and kept; nothing shows it today.

## The names

The tokens are declared in `PandaEditorKit/include/PandaEditorKit/Widgets/Icons.hpp` (`Ic::…`),
one per PNG of the editor. For the current preview:

| Group | Names |
|---|---|
| Transport and edits | `play`, `stop`, `simulate`, `undo`, `redo`, `save`, `reload`, `export`, `plus`, `clear`, `copy`, `trash`, `more-h` |
| Gizmo | `gizmo-move`, `gizmo-rotate`, `gizmo-scale`, `space-local`, `space-world`, `maximize`, `restore` |
| Navigation | `home`, `folder`, `folder-open`, `chevron-left`, `chevron-right`, `chevron-up` |
| Objects and assets | `entity-cube`, `light-sun`, `light-point`, `camera`, `mesh`, `material`, `sprite`, `script`, `audio`, `globe` |
| Status | `info`, `warning`, `error` |
| Settings and tools | `tune`, `configure`, `hammer`, `plugins` |
| Panels (the visibility strip) | `panel-hierarchy`, `panel-inspector`, `panel-content-browser`, `panel-asset-manager`, `panel-output`, `panel-statistics`, `panel-profiler`, `panel-world-settings`, `panel-viewport`, `panel-cmake`, `panel-bottom-dock` |
| File previews (168×168) | `file`, `file-png`, `file-cpp`, `asset-world`, `prefab` |
| Terrain module | `terrain`, `flora-tree`, `flora-bush`, `flora-grass-mesh`, `flora-tuft`, `flora-rock` |

Modules bring their own icons (the Terrain group above) and a plugin replaces them the same way.

## Working on icons

Install the folder as a dev plugin; every icon it names changes at once. Edit a PNG and press
Reload on the card. An icon that does not change has a name the editor does not use — compare it
with the table and the folder of originals. Ship the icons together with a theme in one plugin
when they belong together: the card then shows both tags, Themes and Icons.
