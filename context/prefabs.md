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
- **An addition**: an entity of the file whose `parent` names an entity of an instance — a
  plain entity, or the entry of an instance of another prefab — is something the instance was
  given in the world (`Prefabs::additionOf`). The instance differs from its prefab by it, with
  everything under it, as by an override: it is counted, marked, applied and reverted like one.
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

The code is in four parts (`Panda/src/Panda/Prefabs/`, what they share in
`PrefabsInternal.hpp`): `PrefabJson` — the JSON a patch is made of and the patch of an object;
`PrefabTemplates` — the templates, a prefab as its instances are compared with it, and what
follows a prefab that changed; `PrefabEntries` — an entry ↔ the entities of an instance, and the
files; `Prefabs` — an instance in a world: what it differs in, Revert, Apply, making and
unpacking one.

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
  command `InstanceEntryCommand` holding the two texts, so it is an undo step like any edit. The
  Revert of a whole instance takes its additions out in the same step (a `CompositeCommand` with
  the deletion); an addition alone is reverted by deleting it.
- **One operation takes a difference back** (`strip`, `Prefabs.cpp`): places — an entity of
  a prefab by its path, and a mask of what of it (`{"pointLightComponent": {"intensity":
  true}}`, `{"*": true}`, `{"removed": true}`, `{"parent": true}`) — are taken out of an entry.
  Revert, Restore and Put Back are places of their scope in the entry of their instance; Apply
  Forced is the places Apply wrote (`Applied::places`) taken out of every other entry of the
  project — the same Revert, at the same places, in each instance. An entry of a prefab that
  nests the applied one, or is a variant of it, has the place by a longer path
  (`correspondsTo` follows the nested instances and the roots that are instances of a base).
  The name and the place of a root are never taken.
- **What differs, for the editor**: `differenceOf(entity)` — components added, removed and
  changed, the changed ones with the paths of the fields; on the root also the entities the
  instance removed and its additions; `differenceCount(root)`.
- A prefab that changed — saved in the editor, changed on disk, written by an Apply — reaches
  the open documents at once (`Prefabs::prefabChanged`): for every instance of it (and of the
  prefabs that nest it) the entry is read against the old template, the templates are read
  again, and the instance is brought to its entry over the new one. The dirty flags stay: the
  files hold the same entries. Files that are not open need nothing: they hold the difference.
- **Apply** (`Prefabs::apply(root, scope, fileText)`) writes the part of the difference the
  scope names into the prefab: the file is loaded into a world of its own, the patches are put
  into its entities, and it is written the way a prefab is saved. With the whole instance the
  moves are made there, then the entities it removed leave the prefab, and its additions become
  entities of the prefab: copied under the entity they stand under (`World::importEntity` —
  an instance of another prefab stays one and is nested from now on; a part of the instance
  moved under an addition is moved, not copied), what they name of the instance named by the
  ids of the prefab. In the world the additions become parts of the instance with the ids of
  parts (`Applied::reidentified`: the editor starts the undo history of the world again). A scope
  that names an addition applies it alone. An addition that is — or holds — an instance of a
  prefab that holds this one is refused (`Applied::refused`): the prefab would hold itself.
  **Apply Forced** also takes the written places out of every other instance of the prefab —
  in the open documents in memory (`prefabChanged(…, &applied)`), in the files of the project
  that are not open as text (`Prefabs::stripApplied`). Apply writes files: it is not an undo
  step. The editor side is one call, `FileDocuments::applyToPrefab`: it reads and writes the
  prefab file, rewrites the closed files for Apply Forced before the templates are read again,
  lets the open documents follow, and shows the new file in the tab of the prefab.
- `createFrom` + `convertToInstance` (Create Prefab: the entities keep their ids in the new
  file, the root keeps its id in the world, the entities under it get derived ids and what
  named them names them still); `unpack` (the mark goes; an instance nested in it becomes an
  instance of its own).

## The editor

- A `.prefab` opens in a tab of the center like a world (`WorldFile` with `isPrefab()`): the
  viewport shows it alone, it has its own undo history and Save. It is not played: Play is off
  while a prefab is in front. The document holds one entity that is not of the file — a sky to
  light the stage — which is never saved.
- The tab of a prefab has a tree of its own, as a world tab has; it starts at the root entity
  of the prefab, and that root cannot be deleted, duplicated or moved out.
- In the Hierarchy the entities of an instance are drawn in the accent color, its root with
  the sign of a prefab; an addition keeps the look of a plain entity with a `+` over the lower
  corner of its icon (the topmost entity of the addition).
- The form of an entity of an instance has a strip under the name: the prefab (a click opens
  it), for a part — the instance it is in, how much differs, Revert and Apply (on the root —
  for the whole instance, on a part — for that entity), and a line for what has no row: a
  component or an entity the instance took off (Restore), a part that was moved — under
  another entity of the instance, of the world or of another instance (Put Back: its parent and
  its transform of the prefab again), an addition (Remove). The strip of an addition says what
  it was added to, with Revert (out of the instance) and Apply (into the prefab). A
  row whose field differs is marked (`PropertyRow::setOverridden`: the accent color and a bar;
  `"overridden"` as its accessibility value), a section whose component differs by its title.
  Revert, Apply and Apply Forced are in the menu of a section (the component) and in the
  right-click menu of a marked row (the field).
- **A row is found by the field it names** (`PropertyRow::field`, the key of the field in the
  file: every `add…Row` of the form kit takes it). One pass over the rows of a section
  (`EntityEditorView::markSection`) gives both states a row has against other values of its
  field: mixed — the targets differ in it (`ComponentFields` reads the text of the component
  of every target) — and overridden — the instance has it of its own (`Difference::differs`).
  It runs when the form is built and at every step of the history of the world.
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
as it was, a changed prefab reaching its instances, Revert, Put Back, Apply, Apply Forced in a
file and in an open world through a nested prefab, additions — counted, applied whole and one
by one, an added instance nested by Apply, the refusal of an addition that would make a prefab
hold itself — copies with their own references, the refusal of a prefab that holds itself. The
UI tests hold two scenarios across the editor: an instance that follows its prefab except in
what is its own, and an addition that Apply gives to every instance. The colliders of
the trees of a prefab layer are a smoke check too: a ray stops on the trunk where a tree stands.

## Open items

- The name and the Static toggle of a part are not marked; the strip counts them. The rows of
  the transform keep their own typed mixed state (the rotation is shown as angles, the file
  holds a quaternion).
- A prefab dropped into the viewport stands where the root of the prefab says, not under the
  pointer (a dropped model does the same). The Hierarchy takes no drop of a prefab.
- The Content Browser shows a prefab by the sign of its kind, not by a picture of it.
- Create Prefab, Unpack and an Apply that makes additions parts clear the undo history of the
  document (they change ids). Apply is not undone: the file is written. Apply Forced rewrites
  the closed files at once and leaves the open ones dirty: a document closed without saving
  keeps its overrides.
- A part of an instanced model whose material is blended is drawn with the opaque meshes,
  unsorted. A material on a shader of the project without an instanced variant cannot be drawn
  as instances (the same holds for the mesh layers of the flora).
- The colliders of the trees follow the prefab and the tree points when the world starts to
  run, not while it runs (the relief is followed). A tree with a triangle mesh collider costs a
  copy of the triangles per tree: a tree wants a capsule, a box or a convex mesh.
- A light in a particle prefab does not light: a system takes the meshes, and the terrain the
  colliders. Per-copy animation (a butterfly that beats its wings) is not there.
