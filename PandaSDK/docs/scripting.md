# Scripting

Scripts are C++ classes in `Scripts/<ProjectName>/src/`; they are built into a dynamic library
and loaded by the editor/runtime. The entry points and the API are PandaSDK (the `Bamboo/*`
headers).

## Script skeleton

```cpp
#include <Bamboo/Script.hpp>
#include <Bamboo/Bamboo.hpp>

using namespace Bamboo;

class MoveScript : public Script {
public:
    float speed = 2.f;
    int lives = 3;

    PANDA_FIELDS_BEGIN(MoveScript)
    PANDA_FIELD(speed)
    PANDA_FIELD(lives)
    PANDA_FIELDS_END

    void start() override;
    void update(float deltaTime) override;
};

REGISTER_SCRIPT(MoveScript)
```

- `REGISTER_SCRIPT(Class)` makes the class visible to the engine (the class name = `name` in the
  world's `scriptListComponent`).
- Lifecycle (all methods are optional): `start`, `fixedUpdate(step)`, `update(dt)`,
  `lateUpdate(dt)`, `beginCollisionTouch(other)` / `endCollisionTouch(other)`,
  `beginSensorOverlap(sensor)` / `endSensorOverlap(sensor)`, `shutdown`.
- Physics runs with a fixed step (60 per second). `fixedUpdate` is called before every step, with
  the step length — zero, one or several times a frame: forces, impulses, motor targets, the
  character mover and everything that drives bodies belong there, and the transforms of bodies
  hold their exact poses. `update` and `lateUpdate` run once a frame with the frame time:
  cameras and everything that is drawn. A body's transform in `update` is its pose interpolated
  for the frame, a step behind the simulation, so that it moves smoothly at any frame rate.
- Time. `deltaTime` of `update` is the game time of the frame: the real time, but no more than
  four physics steps (1/15 s). On a slower frame the whole game slows down together — physics
  and timers alike. `ApplicationAPI::getTime()` is the real time of the application, not a game
  clock.
- Input. `isKeyPressed` is the state of a key. `isKeyJustPressed` reports a press once: in
  `update` during the frame of the press, in `fixedUpdate` by exactly one step — so a one-shot
  action can be read where it is used.
- `WorldAPI::destroyEntity` may be called from any callback, for any entity, the script's own
  included: the entity is destroyed after the callbacks that are running now, and until then it
  is still in the world.
- A transform set from a script is a teleport: the body stops and nothing draws the way from the
  old place. The bodies of the entities under it are teleported with it: a car whose wheels are
  children of the chassis is moved by moving the chassis. `CharacterMoverAPI::move` from
  `fixedUpdate` is a continuous motion, drawn smoothly between the steps.
- `TransformComponentAPI` works in the space of the parent. A body may be a child of any entity:
  the simulation moves every body on its own, whatever stands above it, and keeps the transform
  of a child relative to its parent.

## Fields

`PANDA_FIELD(name)` publishes a class member: it appears in the editor's inspector and gets its
value from the world (`scriptFields`). Supported: `int`, `float`, references to an
entity/texture/material/world (the id types `EntityHandle`, `TextureHandle`, `MaterialHandle`,
`WorldHandle`).

The world is the only source of a published field: when Play starts the engine writes every field
of the script from the world, and a field the world does not list is written as zero. The
initializer in the class (`float speed = 2.f`) is therefore not a default — it only documents a
sensible value.

An edit in the inspector during Play is written into the running script at once, so a value read
in `update` can be tuned while the game runs; a value used only in `start` has to be applied
again by the script. Play runs on a copy of the world: Stop brings the values back, and Save
(Cmd+S) during Play saves the world being edited, not the copy. A value found in Play is typed in
again after Stop.

## Manifest for tools: `.Panda/scripts-manifest.json`

On every (re)load of the script bundle the editor writes out a manifest:

```json
{ "scripts": [ { "name": "MoveScript", "scriptFields": [
    { "fieldId": 123456789, "name": "speed", "type": "float" } ] } ] }
```

This is where an agent takes the exact `name`/`fieldId`/`type` for binding values in `.world`
(see world-format.md) — there is no need to compute `fieldId` by hand. The file does not exist
until the scripts have been loaded in the editor at least once.

## Build and reload

- From the editor: the CMake panel (configure/build; after a successful build the fresh library
  is picked up automatically).
- From a terminal: `Scripts/<ProjectName>/scripts/configure_scripts.sh debug` and
  `build_scripts.sh debug` (the SDK paths come from the environment, see the scripts). Then, in
  the editor — **Reload** (Cmd/Ctrl+R): it picks up the fresh library (by mtime) and, along with
  it, changed assets and the world. Running the game and debugging are always left to a human.
