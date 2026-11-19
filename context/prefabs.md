# Prefabs

A prefab is a file with a tree of entities that worlds — and other prefabs — place any number of
times. A placed prefab (an instance) follows its file: what the file changes, every instance
gets, except what the instance itself changed. The user-facing page is
`PandaSDK/docs/prefabs.md`; the file format is in `PandaSDK/docs/world-format.md`.

## The model in one paragraph

An instance exists in two forms. **In a file** (a `.world`, a `.prefab`) it is one entry: the
id of its root, the prefab it is made of, and what it differs in — nothing else of the prefab
is copied into the file. **In memory** — in a `World` — it is ordinary entities with ordinary
components, every one of them marked with where it came from (`PrefabComponent`). The two forms
are turned into one another when a file is read and written, by two functions over JSON
(`Prefabs::expand`, `Prefabs::collapse`). So nothing in the engine that works with a world —
the renderer, the physics, the scripts, the inspector, the undo history, the copy of Play —
knows about prefabs, and a game carries none of this: the cook writes the worlds of a game
expanded, as plain entities.

## Files

- **`.prefab`** — an asset of the kind `prefab` (`AssetType::PREFAB`): a fragment of a world
  file, `{"entities": [...]}`, with exactly one root (an entity whose parent is 0). No settings
  sections. Everything a world entity may be, a prefab entity may be.
- **An instance** — in the `entities` of a world or of a prefab (a prefab that holds instances
  of other prefabs is a nested prefab; a prefab whose root is an instance of another one is its
  variant):

  ```json
  {
      "id": 4100100001,
      "prefab": 3100002001,
      "relationshipComponent": { "parent": 0, "children": [] },
      "tagComponent": { "tag": "Hall Lamp" },
      "transformComponent": { "position": {"x": 3, "y": 0, "z": 1}, "rotation": {"x": 0, "y": 0, "z": 0, "w": 1} },
      "overrides": [
          { "entity": 78, "pointLightComponent": { "intensity": 20 } },
          { "entity": [90, 12], "scriptListComponent": { "scripts": { "Wheel": { "scriptFields": { "grip": { "value": 0.8 } } } } } },
          { "entity": 79, "removed": true },
          { "entity": 80, "parent": 78 }
      ]
  }
  ```

  `prefab` is the id of the prefab asset. The component keys of the entry are what the ROOT of
  the instance differs in from the root of the prefab; `overrides` says the same for the
  entities under it, each named by its id in the prefab file — for an entity of a prefab nested
  in it, by the ids on the way down (`[id of the nested instance in this prefab, id in its
  prefab]`).
- **What differs is a patch of the entity object** — the object a world file would hold for the
  entity, with these rules (`Prefabs::diff`, `Prefabs::merge`):
  - an object is compared key by key: a key of the patch replaces or adds, `null` removes (a
    component the instance took off is `"pointLightComponent": null`, one it added is the whole
    component);
  - an object whose keys are among `x`, `y`, `z`, `w` (a vector, a quaternion, a color) is one
    value;
  - an array of objects that all have a `name`, the names different (the scripts of an entity,
    the fields of a script), is compared by name and written in the patch as an object by name;
    any other array is one value.
- `"removed": true` — the instance has no such entity (with everything under it that was not
  moved elsewhere). `"parent": <id>` — it stands under another entity of the world than its
  prefab says.
- **What the root is called and where it stands are the instance's own**: its name, its
  position and its rotation are always in the entry — first, in that order, so that the lines
  of an entry do not move when a value starts or stops being the one of the prefab — and never
  follow the root of the prefab. They are not a difference: Apply does not take them, Revert
  does not give them back, the editor does not mark them. The scale of the root follows the
  prefab like any other value. An entry written by hand may leave them out: the instance has
  what the prefab has until the editor saves the file.
- An entity ADDED under an instance is an ordinary entity of the file whose `parent` names an
  entity of the instance.
- A file lists its entries the way a world lists its entities (the order of the Hierarchy), so
  the same world is the same text.

## Ids

The id of the root of an instance is in the file. The id of every other entity of the instance
is **derived** from it: `derivedEntityId(instance, source)` (`Panda/Prefabs/PrefabComponent.hpp`)
— a fixed 32-bit mix of the id of the root and the id of the entity in the (expanded) prefab.
So an instance costs one id in the file, a reference to an entity inside an instance (a script
field, a joint, the parent of an added entity) is a number that stays the same on every machine
and in every session, and two games that instantiate the same prefab under the same root id
agree on the ids of its parts. A derived id that is taken (the chance is that of two random
32-bit ids) is mixed again with the number of the attempt (`freeDerivedEntityId`); the
instances of a file are expanded in the order of their ids and the entities of a prefab in the
order of theirs, so the outcome depends on the ids alone. `PandaSDK/docs/world-format.md` gives
the formula, for a tool that writes a reference into an instance.

A reference between two entities of one prefab (a wheel that names its chassis) is written in
the prefab with the ids of the prefab; an instance gets it pointing at its own parts. Which
fields of a component are references to entities, the component type says
(`ComponentType::entityRefs`, a visitor over the ids): the scripts (fields of the type entity)
and the joints of the Physics module do, and so does `PrefabComponent` itself. The same hook
makes a duplicate and a paste point at their own copies.

## In memory

- `PrefabComponent` (`Panda/Prefabs/PrefabComponent.hpp`) on every entity that came from a
  prefab: `instance` — the id of the root entity of its instance, `source` — its id in the
  expanded prefab, `prefab` — on a root (of the instance, or of an instance nested in it) the
  prefab asset. An entity whose `instance` is its own id is the root of an instance of a file.
  In the expanded text of a world (what `Prefabs::expand` gives, what the mapper reads) it is
  the key `prefabLink`; no file of a project holds it.
- **The template** of a prefab is a `PrefabAsset` (`Panda/Prefabs/PrefabAsset.hpp`): a `World`
  that holds the entities of the prefab, expanded (its nested instances are entities there).
  The editor's asset handler makes it from the `.prefab` file (`Prefabs::loadTemplate`), the
  runtime's from the cooked one.
- **Copies are one operation** (`World::copySubtree`, private): `duplicateEntity`,
  `importEntity` and `instantiate(prefab, rootId, placement)` all copy a tree of entities —
  new ids, the references between the copied entities pointing at the copies — and settle it
  the same way: placed first (`EntityPlacement`: a parent, a position, a rotation), then, in a
  running world, bodies made and scripts created and started. A copy of an instance is an
  instance; a copy made from a template is one too (in a game the mark is simply not there:
  the cook strips it).

## Authoring (`Panda/Prefabs/Prefabs.hpp`, compiled with the editor API only)

- `Prefabs::load(world, text)` / `Prefabs::save(world, root)` — a world from the text of its
  file and back; `expand` / `collapse` are the same as text → text. Expanding puts the patches
  of an entry over the entities of the prefab; collapsing compares every entity with its source
  in the prefab. **Overrides are not kept anywhere: they are the difference, found when the
  file is written.** A value set back to what the prefab has is not an override.
- A prefab the project does not have (a deleted file): the entry is kept as it was read and
  written back; the world gets a lone entity in its place ("Missing Prefab"). A prefab that
  holds itself, directly or through another, is refused when it is read — that instance is left
  out, with an error — and the editor refuses to place one (`Prefabs::dependsOn`).
- **One instance as text**: `Prefabs::entryOf(root)` is the entry a save would write for a live
  instance, `Prefabs::setEntry(root, entry)` brings the live instance to an entry in place —
  the ids stay, a part the entry has no more is buried (the undo history may hold it) and found
  again when an entry has it back. Everything that changes an instance as a whole goes through
  this pair: **Revert** at any grain (`entryWithout(root, scope)` — the whole instance, an
  entity, a component, a field by its path; `entryRestoring` for a removed entity) is the
  command `InstanceEntryCommand` holding the two texts, so it is an undo step like any edit.
- **What differs, for the editor**: `differenceOf(entity)` — components added, removed and
  changed, the changed ones with the paths of the fields; on the root also the entities the
  instance removed; `differenceCount(root)`.
- A prefab that changed — saved in the editor, changed on disk, written by an Apply — reaches
  the open documents at once (`Prefabs::prefabChanged`): for every instance of it (and of the
  prefabs that nest it) the entry is read against the old template, the templates are read
  again, and the instance is brought to its entry over the new one. The dirty flags stay: the
  files hold the same entries. Files that are not open need nothing: they hold the difference.
- **Apply** (`Prefabs::apply(root, scope, fileText)`) writes the part of the difference the
  scope names into the prefab: the file is loaded into a world of its own, the patches are put
  into its entities, and it is written the way a prefab is saved. With the whole instance the
  entities it removed leave the prefab and the plain entities added under it become entities of
  the prefab (and parts of the instance). **Apply Forced** also takes the applied keys out of
  every other instance of the prefab — in the open documents in memory
  (`prefabChanged(…, &applied)`), in the files of the project that are not open as text
  (`Prefabs::stripApplied`). Apply writes files: it is not an undo step.
- `createFrom` + `convertToInstance` (Create Prefab: the entities keep their ids in the new
  file, the root keeps its id in the world, the entities under it get derived ids and what
  named them names them still); `unpack` (the mark goes; an instance nested in it becomes an
  instance of its own).

## The editor

- A `.prefab` opens in a tab of the center like a world (`WorldFile` with `isPrefab()`): the
  viewport shows it alone, it has its own undo history and Save. It is not played: Play is off
  while a prefab is in front. The document holds one entity that is not of the file — a sky to
  light the stage — which is never saved.
- The tab of the Hierarchy is titled with the file of the document in front
  (`DockPanel::caption`); for a prefab the tree starts at the root entity of the prefab, and
  that root cannot be deleted, duplicated or moved out.
- In the Hierarchy the entities of an instance are drawn in the accent color, its root with
  the sign of a prefab; an entity added under an instance is not.
- The form of an entity of an instance has a strip under the name: the prefab (a click opens
  it), for a part — the instance it is in, how much differs, Revert and Apply (on the root —
  for the whole instance, on a part — for that entity), and a line for what has no row: a
  component or an entity the instance took off (Restore), a part that was moved — under
  another entity of the instance, of the world or of another instance (Put Back: its parent and
  its transform of the prefab again). A
  row whose field differs is marked (`PropertyRow::setOverridden`: the accent color and a bar;
  `"overridden"` as its accessibility value), a section whose component differs by its title.
  Revert, Apply and Apply Forced are in the menu of a section (the component) and in the
  right-click menu of a marked row (the field). Rows are matched to fields by the name the
  form shows.
- The commands: `prefab.create`, `prefab.unpack`, `prefab.open`, `prefab.revert`,
  `prefab.apply`, `prefab.applyForced`, `prefab.addToWorld` — the palette, the keymap and the
  `command` tool of the MCP server run them. A prefab is placed by a drop into the viewport,
  **Add to World** in the Content Browser, or the command; Duplicate, Copy and Paste of an
  instance give instances (the clipboard holds entries).
- The MCP server: `open_world` opens a prefab, `status` marks the document `"prefab": true`;
  the rest of the tools work on the document in front, whichever it is.

## Scripts

A field of the type `EntityHandle` takes an entity of the world or a prefab (the value is then
the id of the prefab asset; the well of the form takes a drop of either and offers "Prefab…"
in its list). `Bamboo::WorldAPI::instantiate(original, …)` makes a copy of what the handle
names: a new instance of a prefab, or a duplicate of an entity of the world; the host side is
`world_Instantiate` of `ScriptHostApi`. A world that runs holds the prefabs it instantiated
until it stops.

## Copies that are not entities (`Panda/Renderer/InstancedModel`)

A system that places a prefab many times — the particles of an emitter, the trees of a terrain
layer — does not make entities: it takes of the template what it can place cheaply.

- `InstancedModel::ofPrefab(prefab)` is the look of a prefab: every mesh its entities draw (the
  same list a world makes of its own for a frame, `RenderSystem::MeshList` over the template
  world), each with its **place** under the root — the position and the rotation of the root
  left out (a copy stands where the system puts it), its scale kept. The system keeps one
  stream of instance matrices per place — where a copy stands × the place — and
  `InstancedModel::submit` draws the meshes of a place with it (`Renderer3D::submitInstanced` of
  a part: its own material → the slot of the imported mesh → the instanced lit one).
  `ofMesh(mesh, material)` is the same thing for one imported mesh: one place, so one stream —
  the flora draws mesh layers and prefab layers by the same code, and `FloraImposter` bakes the
  billboards of either from the parts.
- `stamp()` is what the model was made of; `PrefabAsset::serial()` — a number no other loaded
  template has, this one loaded again included — makes the stamp of a prefab change when the
  prefab is read again, so the systems follow an edit of the prefab like the instances do.
- The particles (`Modules/Particles`): `prefab` of the emitter; the streams are filled once a
  frame in the scene pass and drawn by it and by the shadow passes.
- The trees (`Modules/Terrain`): `prefab` of a tree layer. The root stands on the ground (an
  imported mesh stands on the bottom of its box). **The trees collide**: `TerrainPhysics` walks
  the template for the entities that carry a collider and gives them, for every tree, to
  `Physics3D::addStaticShapes` — shapes of the static body of the terrain at the place, the
  turn and the size of the tree. Made when the world starts to run, again by cell of the tree
  grid where the relief is edited; a hit names the terrain entity.
- What a copy of this kind does not have is everything else: scripts, lights, bodies, sounds.
  A system that wants more of a prefab reads its template itself, as the terrain does.

## The game

The cook reads worlds with `Prefabs::load`, takes the marks off and writes plain entities: a
prefab that only stands in worlds is not in the game. One a script field, a particle emitter or
a tree layer of a terrain (anything the cook follows as a reference to an asset) names is cooked to `Prefabs/<path>.pprefab` — its
tree, expanded, in binary — and the runtime's asset handler loads it into a `PrefabAsset`.

## Checks

The smoke test of the engine (`Panda/Smoke/Main.cpp`, "prefabs") holds what no scene shows:
the two forms round-trip to the same text, derived ids, patches by path, a missing prefab kept
as it was, a changed prefab reaching its instances, Revert, Apply, copies with their own
references, the refusal of a prefab that holds itself. The UI tests hold one scenario across
the editor: an instance that follows its prefab except in what is its own. The colliders of
the trees of a prefab layer are a smoke check too: a ray stops on the trunk where a tree stands.

## Open items

- The marks of the form are found by the name of a row, so a row a module titles differently
  from its field is not marked (its section still is). The name and the Static toggle of a part
  are not marked; the strip counts them.
- A prefab dropped into the viewport stands where the root of the prefab says, not under the
  pointer (a dropped model does the same). The Hierarchy takes no drop of a prefab.
- The Content Browser shows a prefab by the sign of its kind, not by a picture of it.
- Create Prefab and Unpack clear the undo history of the document (they change ids). Apply is
  not undone: the file is written.
- A part of an instanced model whose material is blended is drawn with the opaque meshes,
  unsorted. A material on a shader of the project without an instanced variant cannot be drawn
  as instances (the same holds for the mesh layers of the flora).
- The colliders of the trees follow the prefab and the tree points when the world starts to
  run, not while it runs (the relief is followed). A tree with a triangle mesh collider costs a
  copy of the triangles per tree: a tree wants a capsule, a box or a convex mesh.
- A light in a particle prefab does not light: a system takes the meshes, and the terrain the
  colliders. Per-copy animation (a butterfly that beats its wings) is not there.
- The editor does not see a script build tree that was configured against another SDK
  (a project copied from another machine): the scripts fail to load until the build folder is
  removed.
