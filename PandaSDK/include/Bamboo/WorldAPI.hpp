#pragma once

#include "Base.hpp"

#include <optional>
#include <vector>

namespace Bamboo {

namespace WorldAPI {
    void load(WorldHandle world);
    EntityHandle findByTag(const char *tag);
    std::vector<EntityHandle> findAllByTag(const char *tag);
    EntityHandle createEntity(const char *tag);
    // A copy of what the handle names, with everything under it. A prefab — a field of the type
    // EntityHandle takes one in the inspector — gives a new instance of it, as a root of the
    // world; an entity of the world gives its duplicate, next to it. The copy is whole when the
    // call returns: its bodies are in the simulation and its scripts have had their `start`.
    // An invalid handle comes back when the original is neither.
    EntityHandle instantiate(EntityHandle original);
    // The same under a parent, and at a place: the position and the rotation of the root of the
    // copy in the space of its parent (of the world, without one), as TransformComponentAPI has
    // them. What is not given stays as the original has it. The copy is at its place before its
    // scripts start.
    EntityHandle instantiate(EntityHandle original, EntityHandle parent);
    EntityHandle instantiate(EntityHandle original, Vec3 position);
    EntityHandle instantiate(EntityHandle original, Vec3 position, Quat rotation);
    EntityHandle instantiate(EntityHandle original, EntityHandle parent, Vec3 position);
    EntityHandle instantiate(EntityHandle original, EntityHandle parent, Vec3 position, Quat rotation);
    // The entity (with its children) is destroyed when the callbacks that are running now are
    // over: a script may destroy any entity, its own included, from any callback. Until then the
    // entity is still in the world.
    void destroyEntity(EntityHandle entity);
}; // namespace WorldAPI

} // namespace Bamboo
