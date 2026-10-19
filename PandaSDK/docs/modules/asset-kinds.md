# Asset kinds

An asset kind is a kind of file the project registry knows: how it is recognized, where it
lives, how it is loaded into an `Asset` object and written back. The core kinds (textures,
shaders, materials, meshes, worlds, postprocessing) have a cook step and handlers of their own;
a module kind is a file of one format that the editor and the runtime read the same way, and
the export copies as it is.

## The asset class

```cpp
class Terrain2DAsset : public Asset {
public:
    explicit Terrain2DAsset(TerrainData2D data, FloraSettings flora = {}, TerrainTrees trees = {});
    …
#ifdef PND_EDITOR
    bool isChanged() const override { return m_changed; }   // the "unsaved" dot of the tab
    void setChanged(bool changed) override { m_changed = changed; }
#endif
};
```

An `Asset` is an in-memory object owned by the asset handler and referenced by `AssetRef<T>`
(the id; `get()` resolves it, `isValid()` tells whether it is loaded). The constructor takes
the kind id: `Asset(Terrain::TYPE_2D)`. Under `PND_EDITOR` the asset is a document — the
editor sets `setChanged(true)` on edits and `false` on save; an asset without an editor keeps
the defaults.

An asset created in code without a file (a system map, a procedural mesh) is
`AssetHandler::createRuntimeAsset<T>(args…)`: an id outside the project space, no registry
entry; the owner must hold the returned `AssetRef`, or the next cleanup frees it.

## The registration

```cpp
AssetKind kind;
kind.name = "terrain2d";            // the kind string of the editor: pickers, D&D payloads, AssetKindUi
kind.type = Terrain::TYPE_2D;       // the numeric id persisted in .pmeta and the runtime registry
kind.displayName = "Terrain 2D";
kind.folder = "Terrains";           // the default folder in the project and in the export
kind.extensions = {".terrain"};
kind.detect = detect2D;             // two kinds share .terrain: the file header tells them apart
kind.load = load2D;                 // Asset *(Foundation::AllocatorI *, const path_t &): nullptr — unreadable
kind.placeholder = placeholder2D;   // Asset *(Foundation::AllocatorI *): shown instead of an unreadable file
kind.save = save2D;                 // bool (Asset &, const path_t &): the document to the file
extensions.assetKinds.add(kind);
```

- `load` allocates with the handler's allocator: `F_NEW(allocator, Terrain2DAsset)(…)`.
  It is called by the editor when the project is scanned or the asset is first used, and by
  the runtime from the cooked copy of the file.
- `placeholder` keeps a broken reference usable (a flat terrain, an empty mesh); the editor
  shows the asset as missing.
- `save` is called by the document model when the user saves the tab; a kind without `save`
  is read-only.
- `detect` runs only among kinds that share an extension, in registration order; the first
  that confirms wins. A kind with its own extension leaves it `nullptr`.
- `fileBased()` is `load != nullptr`; the core kinds are not file based from the registry's
  point of view.

## Ids

`type` is a number written into every `.pmeta` sidecar and into the runtime registry: it
never changes. A new kind takes `AssetKind::idOf(name)`, a stable hash of the name outside
the range of the built-in ids (below 256); the terrain kinds keep the historical small
numbers 9 and 10 because projects already carry them. Two kinds with the same id or the same
name are a registration error (asserted).

## In the editor

The file appears in the Content Browser with the generic icon until the editor part registers
its look: `AssetKindUi {kind, icon, color, createEditor}` gives the icon and the color of the
kind in the browser and on tabs, and the form of the asset tab (`AssetEditor`); an
`AssetCreator {title, create(directory)}` adds a "New …" entry that writes an empty file of
the kind. Edits of the asset go as commands into the document's undo stack
(`EditorHost::assetUndoStack(id)`), which the Save of the tab writes with `save`. See
[The editor part](editor-part.md).

An asset the simulation mutates through a script table (the terrain's `setHeights`) is
reported with `GameContext::markRuntimeMutatedAsset(id)`: Stop in the editor reloads it from
the file, so a Play session leaves no trace in the project.

## In the runtime and the export

The export copies a file kind as it is into the cooked data and the runtime loads it with the
same `load`. A kind that needs a different runtime form (a baked, compressed or stripped
variant) does the conversion inside `load` by platform, or keeps two formats inside one file:
there is no cook step for module kinds.
