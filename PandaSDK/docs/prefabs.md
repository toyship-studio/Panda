# Prefabs

A prefab is a file with a tree of entities — a lamp with its bulb and its shade, an enemy with
its body, colliders and scripts — that worlds place any number of times. A placed prefab is an
**instance**. An instance follows its prefab: change the prefab, and every instance in every
world has the change, except where the instance was given something of its own.

A prefab is a file like a world (`.prefab`, JSON, in `Assets`), and like a world it is edited
either in the editor or as text.

## In the editor

**Making one.** In the Content Browser: New → Prefab — a prefab of one entity, open in its tab.
Or from what a world already has: right-click an entity in the Hierarchy → **Create Prefab**. The
entity with everything under it becomes a file in the folder the Content Browser shows, and stands
in the world as the first instance of it.

**Editing one.** A prefab opens in a tab of the center like a world (a double click on the file,
or **Open Prefab** on any entity of an instance). The viewport shows it alone, the Hierarchy —
its tab is titled with the file in front, `Lamp.prefab` or `world.world` — shows its one tree,
and everything that edits a world edits it: the gizmo, the inspector, Add Component, scripts,
Undo, ⌘S. It is not played: Play is off while a prefab is in front. A prefab has exactly one
root, and everything in it stands under that root.

**Placing one.** Drag the file from the Content Browser into the viewport, or right-click it →
**Add to World**. Duplicate (⌘D) and copy and paste of an instance give instances.

**What you see.** In the Hierarchy the entities of an instance are drawn in the accent color,
its root with the sign of a prefab: that part of the tree is kept in another file. An entity
you add under an instance is an ordinary entity of the world and looks like one.

**Giving an instance something of its own.** Edit it like anything else: move a part, change a
field, add or remove a component, delete an entity of it, put an entity under it. The form of
an entity of an instance says which prefab it belongs to, and marks what differs from the
prefab — a bar and the accent color on the row of a field, the accent color on the title of a
section, a line in the strip under the name for what has no row (a component or an entity the
instance took off). What is not marked comes from the prefab and will follow it.

What an instance is called and where it stands — the name, the position and the rotation of
its root — are always its own: they are not marked, Apply does not take them into the prefab and
a prefab that renames its root does not rename its instances.

**Taking it back, or giving it to the prefab.** The strip under the name of an entity of an
instance has **Revert** and **Apply**; on the root they act on the whole instance, on an entity
under it on that entity. The same two are in the menu of a component section (for that
component) and in the right-click menu of a marked row (for that field).

- **Revert** — the values of the prefab again. An undo step.
- **Apply to Prefab** — what the instance has of its own is written into the prefab file: every
  instance gets it, and this one differs in nothing there any more. With the whole instance,
  the entities it removed leave the prefab and the plain entities added under it become
  entities of the prefab.
- **Apply Forced** — the same, and the other instances lose what they have of their own in the
  same places: all of them show the applied value. The files of the project that are not open
  are rewritten for it; the open ones change in memory and wait for Save.

Apply writes a file, so it is not an undo step; a prefab with unsaved edits is saved first.

**Unpack Prefab** (the Hierarchy menu) cuts the tie: the entities of the instance become plain
entities of the world.

All of it is in the command palette under "Prefab", and so can be given a key (Keymap).

## A prefab in a prefab, and variants

A prefab may hold instances of other prefabs: a street lamp that holds the lamp. What the outer
prefab changes in the inner one is kept in the outer file, like in a world; a change of the
inner prefab reaches the outer one and every instance of it.

A **variant** is a prefab whose root is an instance of another prefab: Create Prefab on the
root of an instance makes one. A red lamp that is "the lamp, with a red light" follows the lamp
in everything but the light.

A prefab cannot hold itself, directly or through another: the editor refuses to place it, and a
file that says so is read with that instance left out and an error in the log.

## From a script

A field of the type `EntityHandle` takes a prefab: drop the file onto the field, or choose
Prefab… in its list. `WorldAPI::instantiate` makes a copy of what the handle names:

```cpp
class Gun : public Bamboo::Script {
public:
    Bamboo::EntityHandle bullet; // a prefab, set in the inspector

    PANDA_FIELDS_BEGIN(Gun)
    PANDA_FIELD(bullet)
    PANDA_FIELDS_END

    void fire(Bamboo::Vec3 muzzle, Bamboo::Quat aim) {
        Bamboo::EntityHandle shot = Bamboo::WorldAPI::instantiate(bullet, muzzle, aim);
        // shot is the root of the new instance: its bodies are in the simulation and its
        // scripts have had their start()
    }
};
```

- `instantiate(original)` — a prefab gives a new instance as a root of the world; an entity of
  the world gives its duplicate, next to it.
- `instantiate(original, parent)`, `instantiate(original, position)`,
  `instantiate(original, position, rotation)`, `instantiate(original, parent, position, rotation)`
  — under a parent, and at a place: the position and the rotation of the root in the space of
  its parent, as `TransformComponentAPI` has them. The copy is at its place before its scripts
  start.
- The copy is whole when the call returns. References between its entities — a script field
  that names another entity of the prefab, a joint — point at the entities of the copy.
- `WorldAPI::destroyEntity(root)` removes an instance like any entity.

A prefab a field names is in the exported game (and one an emitter or a tree layer names —
below); one that only stands in worlds as instances does not need to be — the cook writes the worlds with their instances already as entities
([Exporting a game](export.md)).

## Many copies without entities: particles and trees

An instance is entities, and a thousand of them is a thousand times the entities of the prefab.
Two systems place a prefab many times without making an entity of each copy: they take what
they can place cheaply — its meshes, drawn as instances with one draw call per mesh whatever the
number of copies — and leave the rest of the prefab alone.

- **A particle** (the Particles module): the **Prefab** of a Particle Emitter. Each particle is
  the meshes of the prefab with their materials, lit and casting shadows like the meshes of the
  world. **Orientation** turns it along its flight, gives it a turn of its own that spins, or
  keeps it as the emitter stands; the sizes of the emitter scale the prefab. A swarm of
  butterflies, falling leaves, the chips of what broke.
- **A tree of a terrain** (the Terrain module): the **Prefab** of a tree layer, instead of its
  mesh. The root of the prefab stands on the ground at every point of the layer; far away the
  trees are billboards baked from the prefab. And the trees collide: the colliders of the
  prefab — a capsule on the trunk, a box, a convex mesh — are given to every tree of the layer,
  in the physics of the terrain. What touches a tree touches the terrain entity.

What such a copy does not have: the scripts, the lights, the sounds and the bodies of the
prefab — nothing runs for a particle or a tree. A copy that has to live — to be hit, to fall, to
think — is made with `WorldAPI::instantiate`. A change of the prefab reaches the particles and
the trees at once, like its instances; the colliders of the trees are made when the game starts.

Both are references to an asset like any other: a prefab an emitter or a terrain names is in
the exported game.

## As text

The format is in [The `.world` format](world-format.md#prefabs-and-instances). In short: a
`.prefab` is the `entities` of a world file with one root; an instance in a world is one entry —

```json
{
    "id": 4100100001,
    "prefab": 3100002001,
    "relationshipComponent": { "parent": 0, "children": [] },
    "tagComponent": { "tag": "Hall Lamp" },
    "transformComponent": { "position": {"x": 3, "y": 0, "z": 1}, "rotation": {"x": 0, "y": 0, "z": 0, "w": 1} },
    "overrides": [
        { "entity": 78, "pointLightComponent": { "intensity": 20 } }
    ]
}
```

— the prefab, what the instance is called and where it stands, and what it differs in. Nothing of the prefab is
copied into the world, so a hundred lamps are a hundred short entries, and a change of the
prefab is a change of one file.
