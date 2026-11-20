#pragma once

#include "Panda/GameLogic/Entity.hpp"

#include <Rain/Rain.hpp>
#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Panda {

// Given every reference to an entity a component holds, one at a time; it may write another id
// into it.
using EntityRefVisitor = std::function<void(UUID &)>;

// A component type as a contribution to the engine registry (context/plugins.md). The name is the
// component's key in .world and the only identity for the core; the operations are what the core
// does with a component without knowing its type: create a registry pool (a world copy and an
// entity duplicate go through entt pools without the type), write and read. The core and plugins
// register identically, and in their own systems use entt directly by static type.
struct ComponentType final {
    std::string name;         // key in .world: "transformComponent"
    entt::id_type typeId = 0; // entt::type_hash<T>::value(): component pool in the world registry
    bool serialized = true;   // written to .world (the DeletedComponent tombstone is not)
    bool duplicated = true;   // copied into an entity duplicate (Id and Relationship are special)
    // Every entity has it from its creation (the name, the transform, the place in the
    // hierarchy, the scripts): decode writes into the one that is there. Any other component is
    // ADDED by its decode — it must not be on the entity yet.
    bool builtin = false;
    void (*prepare)(entt::registry &) = nullptr; // create the pool: a typeless copy target must have it
    // The value under name: encode is called when the entity HAS the component; decode — when the
    // stream HAS the key (add the component or update the one created by default).
    void (*encode)(Rain::Encoder &, Entity) = nullptr;
    void (*decode)(Rain::Decoder &, Entity) = nullptr;
    // A default component on the entity / the component off it — by name, for the code that does
    // not know the type (the script host's add/remove by name).
    void (*create)(Entity) = nullptr;
    void (*remove)(Entity) = nullptr;
    // The references to entities the component holds — the scripts: their fields of the type
    // entity; a joint: the body it is connected to. A type that holds none leaves it null. A
    // copy of entities points at its own copies through it (a duplicate, a paste, an instance of
    // a prefab): an id written into a component is a number like any other, and only its type
    // knows that it names an entity.
    void (*entityRefs)(Entity, const EntityRefVisitor &) = nullptr;
};

template<typename T>
ComponentType componentType(
    const char *name, void (*encode)(Rain::Encoder &, Entity), void (*decode)(Rain::Decoder &, Entity)
) {
    ComponentType type;
    type.name = name;
    type.typeId = entt::type_hash<T>::value();
    type.prepare = [](entt::registry &registry) { registry.storage<T>(); };
    type.encode = encode;
    type.decode = decode;
    type.create = [](Entity entity) {
        if constexpr (std::is_empty_v<T>) {
            entity.addTag<T>();
        } else {
            entity.addComponent<T>();
        }
    };
    type.remove = [](Entity entity) { entity.removeComponent<T>(); };
    return type;
}

// Registry of component types: registration order is the order of keys in .world.
class ComponentTypeRegistry final {
public:
    void add(ComponentType type);
    const std::vector<ComponentType> &types() const {
        return m_types;
    }
    const ComponentType *find(std::string_view name) const;
    const ComponentType *find(entt::id_type typeId) const;
    // Every reference to an entity the components of `entity` hold (entityRefs of their types),
    // to read or to change in place; `except` — the type whose references are left out (0 —
    // none): the authoring side of prefabs goes through the references between the entities of
    // an instance without the link of a part to its root (PrefabComponent).
    void forEachReference(Entity entity, const EntityRefVisitor &visit, entt::id_type except = 0) const;
    // Create the pools of all types in the world registry (World::World).
    void prepare(entt::registry &registry) const;

private:
    std::vector<ComponentType> m_types;
};

} // namespace Panda
