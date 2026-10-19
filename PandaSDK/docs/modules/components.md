# Components

A component is a plain struct the module puts on entities; the engine stores it in an entt
pool of the world registry. The core does three things with a component without knowing its
type: it creates the pool, it writes the component into `.world` and reads it back, and it
adds or removes it by name for scripts. All three come from one registration.

## The type

```cpp
// engine/include/Panda/Beacons/BeaconComponents.hpp
#pragma once
#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Base/Base.hpp>

namespace Panda {

// The settings of a beacon. The live state (the blink phase) is system state in
// World::context, not a field here.
struct BeaconComponent final {
    float radius = 2.f;
    Color color{1.f, 0.8f, 0.3f, 1.f};
    AssetRef<Asset> texture; // empty — the built-in white
    bool enabled = true;
};

} // namespace Panda
```

- A component is copyable: the world copy for Play and the entity duplicate copy the pools
  without the type.
- Data the simulation derives (handles, caches, the phase of an animation) stays out of the
  component or sits in it as opaque storage that is not serialized (the colliders of the
  Physics module keep the shape id in `uint8_t runtimeData[8]`). The place for live state is
  the world context of the system ([World systems](world-systems.md)).
- An asset is referenced with `AssetRef<Asset>` (or a typed `AssetRef<TextureAsset>`); the
  reference is the id, the asset is resolved on use (`ref.get()`, `ref.as<T>()`).
- An empty struct is a tag (`IsStaticComponent` of the core); the engine adds it with
  `Entity::addTag<T>()`.

## The DTO and the key

The component is written into `.world` as a JSON value under its own key; a Rain-Codable DTO
is the shape of that value. The DTO mirrors the component field by field, with the same
defaults, and the copy between the two is explicit:

```cpp
// engine/include/Panda/Beacons/BeaconDtos.hpp
#pragma once
#include <Panda/Base/Base.hpp>
#include <Panda/Serialization/GlmCoders.hpp> // glm vectors and matrices as flat arrays
#include <Rain/Rain.hpp>

namespace Panda {

struct BeaconComponentDto : public Rain::Codable {
    float radius = 2.f;
    Color color{1.f, 0.8f, 0.3f, 1.f};
    UUID texture = 0; // an asset reference is written as its id
    bool enabled = true;

    RAIN_FIELDS_BEGIN(BeaconComponentDto)
    RAIN_FIELD(radius)
    RAIN_FIELD(color)
    RAIN_FIELD(texture)
    RAIN_FIELD(enabled)
    RAIN_FIELDS_END
};

} // namespace Panda
```

```cpp
// engine/src/Panda/Beacons/BeaconsExtensions.cpp
namespace {
    void encodeBeacon(Rain::Encoder &encoder, Entity entity) {
        const BeaconComponent &beacon = entity.getComponent<BeaconComponent>();
        BeaconComponentDto dto;
        dto.radius = beacon.radius;
        dto.color = beacon.color;
        dto.texture = beacon.texture.getId();
        dto.enabled = beacon.enabled;
        encoder.encode("beaconComponent", dto);
    }
    void decodeBeacon(Rain::Decoder &decoder, Entity entity) {
        BeaconComponentDto dto;
        if (!decoder.decode("beaconComponent", dto)) { return; } // the entity has no beacon
        BeaconComponent &beacon = entity.addComponent<BeaconComponent>();
        beacon.radius = dto.radius;
        beacon.color = dto.color;
        beacon.texture = AssetRef<Asset>(dto.texture);
        beacon.enabled = dto.enabled;
    }
}

void Panda::Beacons::registerExtensions(Extensions &extensions) {
    extensions.components.add(componentType<BeaconComponent>("beaconComponent", encodeBeacon, decodeBeacon));
}
```

`encode` is called for an entity that has the component; `decode` when the entity in the
file has the key — the function still checks the result and adds the component only then (a
type an entity gets by default, like the core transform, updates the existing component in
`decode` instead of adding one). A field that is not in the file keeps the DTO default: a new
field is a compatible change, a renamed one is not. The key is the identity of the component everywhere — in `.world`, in the editor
registries (`ComponentInspector::component`), in scripts (`EntityAPI::addComponent(entity,
"beaconComponent")`) — and it never changes once a world has been saved with it
([Data and compatibility](data-and-compatibility.md)). Core keys are lowerCamel with the
`Component` suffix; the module's follow.

Why a DTO and not the component itself: the component holds engine types (`AssetRef`, runtime
storage) and the file holds ids; the DTO is the file format, stated once, and the component is
free to change its memory layout.

## What the registration gives

`componentType<T>(name, encode, decode)` fills a `ComponentType`:

| Field | Meaning |
|---|---|
| `name` | the key in `.world` |
| `typeId` | `entt::type_hash<T>::value()`: the pool in the world registry, the identity the hooks and the editor use |
| `prepare` | creates the pool (`registry.storage<T>()`) — a typeless world copy needs every pool to exist |
| `encode`, `decode` | the functions above; `serialized = false` (and both `nullptr`) for a type that is not written |
| `create`, `remove` | `addComponent<T>()` / `addTag<T>()` and `removeComponent<T>()` — the script host adds and removes by name |
| `duplicated` | whether an entity duplicate copies it (true) |

A type that is not written to the file — a runtime-only marker — registers with
`componentType<T>(name, nullptr, nullptr)` and `serialized = false`; it still needs the
registration for its pool.

## Reading components in a system

```cpp
auto view = world.getAllEntitiesWith<IdComponent, BeaconComponent, TransformComponent>();
for (auto handle : view) {
    if (!world.isValidEntt(handle)) { continue; } // a deleted entity (the undo tombstone)
    const UUID id = view.get<IdComponent>(handle).id;
    const BeaconComponent &beacon = view.get<BeaconComponent>(handle);
    const glm::mat4 model = world.getWorldSpaceTransformMatrix(world.entity(handle));
    …
}
```

- `isValidEntt` skips the entities deleted in the editor that the registry still holds for
  undo; every walk over a view needs it.
- `world.entity(handle)` makes an `Entity` from a view handle; `getById(uuid)` finds one by
  id; `calculateWorldSpaceTransform()` and `getWorldSpaceTransformMatrix` give the world pose.
- The hierarchy is read through `Entity` (`getParent`, `getChildEntities`) and changed only
  through `World::setParent`.

## Changing components while the world runs

`Entity::addComponent<T>()` and `removeComponent<T>()` tell the running systems through the
`componentsChanged` hook (the Physics module creates or removes a body when a collider is
added or removed). A value written into an existing component is not reported: a system that
derives state from values reads them again on its own schedule, or its editor part tells it
(the Physics inspector calls `propertiesUpdated` after a commit). Scripts add and remove
components by key through the host; their component APIs read and write the values directly.

## Foreign components

A key of a module that is not registered in the process is kept on the entity as an opaque
JSON value (`ForeignComponents`) and written back as it is, with a warning in the log, so a
world edited without the module loses nothing. Only the JSON coders carry such values; the
binary files of an export hold registered data only ([Data and compatibility](data-and-compatibility.md)).
